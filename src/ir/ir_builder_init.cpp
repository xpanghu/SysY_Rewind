#include "ir_builder.h"
#include "ast.h"
#include "func_context.h"
#include "ir_type.h"
#include "rewind_ir.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rewind_ir
{
namespace
{

inline const IRInt32Type* get_i32_type()
{
    return IRTypeContext::instance().getInt32();
}

inline const IRUnitType* get_unit_type()
{
    return IRTypeContext::instance().getUnit();
}

inline const IRPointerType* get_pointer_type(const IRType* base_type)
{
    return IRTypeContext::instance().getPointer(base_type);
}

inline const IRArrayType* get_array_type(const std::vector<int32_t>& dims, const IRType* elem_type)
{
    if (dims.empty()) {
        return elem_type->is_array() ? elem_type->as<IRArrayType>() : nullptr;
    }

    const IRType* current_type = elem_type;
    for (int i = static_cast<int>(dims.size()) - 1; i >= 0; --i) {
        current_type = IRTypeContext::instance().getArray(current_type, static_cast<size_t>(dims[i]));
    }

    return current_type->as<IRArrayType>();
}

enum class InitEvalMode {
    ConstOnly,
    RuntimeAllowed,
};

struct InitTree {
    struct Scalar {
        const BaseAST* expr = nullptr;
    };

    struct List {
        std::vector<InitTree> children;
    };

    std::variant<Scalar, List> payload;
};

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template <typename T>
const T& expect_node(const BaseAST& node, const char* expected_name)
{
    const auto* p = dynamic_cast<const T*>(&node);
    if (p == nullptr) {
        throw std::runtime_error(std::string("AST type mismatch, expected: ") + expected_name);
    }
    return *p;
}

template <typename T>
const T& must_node(const BaseAST& node, const char* expected_name)
{
    if (const auto* casted = dynamic_cast<const T*>(&node)) {
        return *casted;
    }
    throw std::runtime_error(
        std::string("AST node kind mismatch, expected ") + expected_name);
}

InitTree build_init_tree(const ConstInitValAST& ast)
{
    return std::visit(
        overloaded{
            [&](const ConstInitValAST::ConstExprInit& expr_init) -> InitTree {
                return InitTree{InitTree::Scalar{expr_init.const_exp.get()}};
            },
            [&](const ConstInitValAST::ConstArrayInit& array_init) -> InitTree {
                InitTree::List list;
                list.children.reserve(array_init.const_inits.size());
                for (const auto& item : array_init.const_inits) {
                    const auto& nested = must_node<ConstInitValAST>(*item, "ConstInitValAST");
                    list.children.push_back(build_init_tree(nested));
                }
                return InitTree{std::move(list)};
            }},
        ast.payload);
}

InitTree build_init_tree(const InitValAST& ast)
{
    return std::visit(
        overloaded{
            [&](const InitValAST::ExprInit& expr_init) -> InitTree {
                return InitTree{InitTree::Scalar{expr_init.exp.get()}};
            },
            [&](const InitValAST::ArrayInit& array_init) -> InitTree {
                InitTree::List list;
                list.children.reserve(array_init.inits.size());
                for (const auto& item : array_init.inits) {
                    const auto& nested = must_node<InitValAST>(*item, "InitValAST");
                    list.children.push_back(build_init_tree(nested));
                }
                return InitTree{std::move(list)};
            }},
        ast.payload);
}

size_t total_array_elems(const std::vector<int32_t>& dims)
{
    size_t total = 1;
    for (const auto dim : dims) {
        total *= static_cast<size_t>(dim);
    }
    return total;
}

template <typename Elem, typename MakeZero>
void fill_zero_object(const IRType* type,
                      size_t& cursor,
                      std::vector<Elem>& out,
                      InitEvalMode mode,
                      MakeZero&& make_zero)
{
    if (type->is_int32()) {
        out[cursor++] = make_zero(mode);
        return;
    }

    const auto* array_type = type->as<IRArrayType>();
    for (size_t i = 0; i < array_type->length; ++i) {
        fill_zero_object(array_type->element_type, cursor, out, mode, make_zero);
    }
}

template <typename Elem, typename EvalScalar, typename MakeZero>
void fill_object_from_sequence(const std::vector<InitTree>& items,
                               size_t& item_idx,
                               const IRType* type,
                               size_t& cursor,
                               std::vector<Elem>& out,
                               InitEvalMode mode,
                               EvalScalar&& eval_scalar,
                               MakeZero&& make_zero)
{
    if (type->is_int32()) {
        if (item_idx >= items.size()) {
            fill_zero_object(type, cursor, out, mode, make_zero);
            return;
        }

        const auto& item = items[item_idx];
        if (const auto* scalar = std::get_if<InitTree::Scalar>(&item.payload)) {
            out[cursor++] = eval_scalar(*scalar->expr, mode);
            ++item_idx;
            return;
        }

        size_t nested_idx = 0;
        const auto& nested_items = std::get<InitTree::List>(item.payload).children;
        fill_object_from_sequence(
            nested_items, nested_idx, type, cursor, out, mode, eval_scalar, make_zero);
        ++item_idx;
        return;
    }

    const auto* array_type = type->as<IRArrayType>();
    for (size_t i = 0; i < array_type->length; ++i) {
        if (item_idx >= items.size()) {
            fill_zero_object(array_type->element_type, cursor, out, mode, make_zero);
            continue;
        }

        const auto& item = items[item_idx];
        if (std::holds_alternative<InitTree::List>(item.payload)) {
            size_t nested_idx = 0;
            const auto& nested_items = std::get<InitTree::List>(item.payload).children;
            fill_object_from_sequence(
                nested_items,
                nested_idx,
                array_type->element_type,
                cursor,
                out,
                mode,
                eval_scalar,
                make_zero);
            ++item_idx;
            continue;
        }

        fill_object_from_sequence(
            items,
            item_idx,
            array_type->element_type,
            cursor,
            out,
            mode,
            eval_scalar,
            make_zero);
    }
}

template <typename Elem, typename EvalScalar, typename MakeZero>
void flatten_init_tree_impl(const InitTree& init,
                            const IRType* object_type,
                            size_t& cursor,
                            std::vector<Elem>& out,
                            InitEvalMode mode,
                            EvalScalar&& eval_scalar,
                            MakeZero&& make_zero)
{
    std::vector<InitTree> items = std::visit(
        overloaded{
            [](const InitTree::Scalar& s) -> std::vector<InitTree> {
                return {InitTree{s}};
            },
            [](const InitTree::List& l) -> std::vector<InitTree> {
                return l.children;
            }},
        init.payload);

    size_t item_idx = 0;
    fill_object_from_sequence(
        items,
        item_idx,
        object_type,
        cursor,
        out,
        mode,
        eval_scalar,
        make_zero);
}

} // namespace

void RewindIRBuilder::process_const_array_init(const ConstInitValAST& init_val,
                                               const std::vector<int32_t>& array_dims,
                                               std::vector<int32_t>& target_buffer,
                                               size_t current_dim_idx)
{
    static_cast<void>(current_dim_idx);
    target_buffer.assign(total_array_elems(array_dims), 0);
    size_t cursor = 0;
    auto tree = build_init_tree(init_val);

    const IRType* object_type = get_array_type(array_dims, get_i32_type());

    flatten_init_tree_impl<int32_t>(
        tree,
        object_type,
        cursor,
        target_buffer,
        InitEvalMode::ConstOnly,
        [&](const BaseAST& expr_ast, InitEvalMode) {
            const auto& exp = expect_node<ExpAST>(expr_ast, "ExpAST");
            return eval_exp(exp);
        },
        [&](InitEvalMode) { return 0; });
}

void RewindIRBuilder::process_array_init_const(const InitValAST& init_val,
                                               const std::vector<int32_t>& array_dims,
                                               std::vector<int32_t>& target_buffer,
                                               size_t current_dim_idx)
{
    static_cast<void>(current_dim_idx);
    target_buffer.assign(total_array_elems(array_dims), 0);
    size_t cursor = 0;
    auto tree = build_init_tree(init_val);

    const IRType* object_type = get_array_type(array_dims, get_i32_type());

    flatten_init_tree_impl<int32_t>(
        tree,
        object_type,
        cursor,
        target_buffer,
        InitEvalMode::ConstOnly,
        [&](const BaseAST& expr_ast, InitEvalMode) {
            const auto& exp = expect_node<ExpAST>(expr_ast, "ExpAST");
            return eval_exp(exp);
        },
        [&](InitEvalMode) { return 0; });
}

IRValue* RewindIRBuilder::build_array_aggregate_from_flat(const IRArrayType* array_type,
                                                          const std::vector<int32_t>& flat_values,
                                                          size_t& cursor,
                                                          IRModule& module)
{
    std::vector<IRValue*> elems;
    elems.reserve(array_type->length);

    if (array_type->element_type->is_array()) {
        const auto* child_array_type = array_type->element_type->as<IRArrayType>();
        for (size_t i = 0; i < array_type->length; ++i) {
            elems.push_back(build_array_aggregate_from_flat(
                child_array_type,
                flat_values,
                cursor,
                module));
        }
    } else if (array_type->element_type->is_int32()) {
        for (size_t i = 0; i < array_type->length; ++i) {
            const int32_t value = cursor < flat_values.size() ? flat_values[cursor] : 0;
            elems.push_back(get_or_create_constant(value, module));
            ++cursor;
        }
    } else {
        throw std::runtime_error("unsupported array element type in aggregate builder");
    }

    return module.make_value<IRAggregate>(std::move(elems), array_type);
}

void RewindIRBuilder::process_array_init_runtime(const InitValAST& init_val,
                                                 const std::vector<int32_t>& array_dims,
                                                 std::vector<IRValue*>& target_buffer,
                                                 size_t current_dim_idx)
{
    static_cast<void>(current_dim_idx);
    target_buffer.assign(
        total_array_elems(array_dims),
        get_or_create_constant(0, current_ctx_->module()));
    size_t cursor = 0;
    auto tree = build_init_tree(init_val);

    const IRType* object_type = get_array_type(array_dims, get_i32_type());

    flatten_init_tree_impl<IRValue*>(
        tree,
        object_type,
        cursor,
        target_buffer,
        InitEvalMode::RuntimeAllowed,
        [&](const BaseAST& expr_ast, InitEvalMode) {
            const auto& exp = expect_node<ExpAST>(expr_ast, "ExpAST");
            return lower_exp(exp);
        },
        [&](InitEvalMode) {
            return get_or_create_constant(0, current_ctx_->module());
        });
}

void RewindIRBuilder::local_array_init(IRValue* alloc,
                                       const std::vector<int32_t>& array_dims,
                                       const std::vector<IRValue*>& values)
{
    size_t total_count = 1;
    for (const auto dim : array_dims) {
        total_count *= static_cast<size_t>(dim);
    }

    if (values.size() > total_count) {
        throw std::runtime_error(alloc->name_ + " local_array_init: initializer size mismatch");
    }

    for (size_t flat_index = 0; flat_index < values.size(); ++flat_index) {
        IRValue* current_ptr = alloc;
        const IRType* current_elem_type = get_array_type(array_dims, get_i32_type());
        size_t remainder = flat_index;

        for (size_t dim = 0; dim < array_dims.size(); ++dim) {
            size_t stride = 1;
            for (size_t next = dim + 1; next < array_dims.size(); ++next) {
                stride *= static_cast<size_t>(array_dims[next]);
            }

            const size_t index_value = stride == 0 ? 0 : remainder / stride;
            remainder = stride == 0 ? 0 : remainder % stride;

            current_elem_type = current_elem_type->as<IRArrayType>()->element_type;
            current_ptr = &current_ctx_->create_block_value<IRGetElemPtrInst>(
                current_ptr,
                get_or_create_constant(static_cast<int32_t>(index_value), current_ctx_->module()),
                get_pointer_type(current_elem_type),
                current_ctx_->next_percent_name());
        }

        static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
            values[flat_index],
            current_ptr,
            get_unit_type()));
    }
}

} // namespace rewind_ir
