#include "ir_builder.h"
#include "ast.h"
#include "func_context.h"
#include "rewind_ir.h"
#include "ir_type.h"
#include "symbol_table.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <variant>
#include <vector>

namespace rewind_ir
{
namespace
{

// get i32 type
inline const IRInt32Type* get_i32_type()
{
    return IRTypeContext::instance().getInt32();
}

// get unit type
inline const IRUnitType* get_unit_type()
{
    return IRTypeContext::instance().getUnit();
}

// get pointer type
inline const IRPointerType* get_pointer_type(const IRType* base_type)
{
    return IRTypeContext::instance().getPointer(base_type);
}

inline const IRArrayType* get_array_type(const std::vector<int32_t> dims, const IRType* elem_type)
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

// get function type
inline const IRFunctionType* get_function_type(std::vector<const IRType*> params, const IRType* ret)
{
    return IRTypeContext::instance().getFunction(std::move(params), ret);
}

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

inline const IRType* func_return_type(const FuncType type)
{
    if (type == FuncType::VOID) {
        return get_unit_type();
    } else if (type == FuncType::INT) {
        return get_i32_type();
    }
    throw std::runtime_error("func type only support void / int");
}

} // namespace

IRModule RewindIRBuilder::build(const BaseAST& ast)
{
    // Initialize module
    IRModule module{};

    // Initialize module symbol table
    module_symbols_ = SymbolTable{};
    module_symbols_.enter_scope();

    constant_cache_.clear();

    const auto& comp_unit = expect_node<CompUnitAST>(ast, "CompUnitAST");
    lower_comp_unit(comp_unit, module);

    return module;
}

/*
 * generate IR
 */
void RewindIRBuilder::lower_comp_unit(const CompUnitAST& ast,
                                      IRModule& module)
{
    declare_library_function(module);

    for (const auto& item : ast.items) {
        if (const auto* global_decl = dynamic_cast<const DeclAST*>(item.get())) {
            lower_global_decl(*global_decl, module);
        }

        if (const auto* func_def = dynamic_cast<const FuncDefAST*>(item.get())) {
            declare_function(*func_def, module);
        }
    }

    for (const auto& item : ast.items) {
        if (const auto* func_def = dynamic_cast<const FuncDefAST*>(item.get())) {
            lower_func_def(*func_def, module);
        }
    }
}

IRFunction* RewindIRBuilder::declare_function(const FuncDefAST& ast, IRModule& module)
{
    // set function type
    auto return_type = func_return_type(ast.func_type);

    // function params
    std::vector<const IRType*> param_types;
    param_types.reserve(ast.func_f_params.size());
    for (const auto& item : ast.func_f_params) {
        const auto& func_f_param = expect_node<FuncFParamAST>(*item, "FuncFParam");
        param_types.push_back(lower_func_f_params(func_f_param));
    }

    auto* func_type = get_function_type(param_types, return_type);

    // create function
    auto func = module.make_function(func_type, ast.ident);
    module_symbols_.define_function(ast.ident, func);

    // assign formal args
    // Note : not define variable in symbol table
    size_t param_index = 0;
    for (const auto& item : ast.func_f_params) {
        const auto& func_f_param = expect_node<FuncFParamAST>(*item, "FuncFParam");
        size_t current_param_index = param_index;
        const IRType* param_type = func_type->params[current_param_index];

        auto* arg_ref = module.make_value<IRFuncArgRef>(
            current_param_index,
            param_type,
            "@" + ast.ident + "_" + func_f_param.ident);
        module.append_param(*func, *arg_ref);
        param_index++;
    }
    return func;
}

void RewindIRBuilder::lower_global_decl(const DeclAST& ast, IRModule& module)
{
    if (const auto* const_decl = dynamic_cast<const ConstDeclAST*>(ast.const_or_var.get())) {
        for (const auto& def_base : const_decl->const_defs) {
            const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
            std::visit(
                overloaded{
                    [&](const ConstDefAST::ConstExpr& const_expr) {
                        // get init value
                        const auto& init =
                            expect_node<ConstInitValAST>(*const_expr.const_init_val, "ConstInitValAST");
                        const auto& expr_init =
                            std::get<ConstInitValAST::ConstExprInit>(init.payload);
                        const auto& exp = expect_node<ExpAST>(*expr_init.const_exp, "ExpAST");

                        auto value = eval_exp(exp);
                        module_symbols_.define_const(const_expr.ident, value);
                    },
                    [&](const ConstDefAST::ConstArray& const_array) {
                        if (const_array.const_dims.empty()) {
                            throw std::runtime_error("array must have at least one dimension: "
                                                     + const_array.ident);
                        }

                        const auto& init_val =
                            expect_node<ConstInitValAST>(*const_array.const_init_val, "ConstInitValAST");
                        if (std::holds_alternative<ConstInitValAST::ConstExprInit>(init_val.payload)) {
                            throw std::runtime_error(const_array.ident + "is array, init must be array");
                        }

                        std::vector<int32_t> array_dims;
                        array_dims.reserve(const_array.const_dims.size());
                        for (const auto& item : const_array.const_dims) {
                            const auto& size_exp = expect_node<ExpAST>(*item, "ExpAST");
                            array_dims.push_back(eval_exp(size_exp));
                        }

                        const int32_t length = std::accumulate(
                            array_dims.begin(),
                            array_dims.end(),
                            1,
                            std::multiplies<int32_t>());

                        if (length <= 0) {
                            throw std::runtime_error("array length must be positive : " + const_array.ident);
                        }

                        std::vector<int32_t> target_buffer;
                        target_buffer.reserve(length);
                        flatten_const_array_initializer(init_val, array_dims, target_buffer);

                        auto* array_type = get_array_type(array_dims, get_i32_type());
                        size_t cursor = 0;
                        auto* init_aggregate = build_array_aggregate_initializer(
                            array_type,
                            target_buffer,
                            cursor,
                            module);

                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            init_aggregate,
                            get_pointer_type(array_type),
                            "@" + const_array.ident);

                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(const_array.ident, global_alloc, true);
                    }},
                def.payload);
        }
        return;
    }

    if (const auto* var_decl = dynamic_cast<const VarDeclAST*>(ast.const_or_var.get())) {
        auto* i32_type = get_i32_type();
        auto* i32_ptr_type = get_pointer_type(i32_type);

        for (const auto& def_base : var_decl->var_defs) {
            const auto& def = expect_node<VarDefAST>(*def_base, "VarDefAST");

            std::visit(
                overloaded{
                    [&](const VarDefAST::UninitializedScalar& uninit_scalar) {
                        // set zero to the default init of global value
                        auto* zero_init = module.make_value<IRZeroInit>(i32_type);
                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            zero_init,
                            i32_ptr_type,
                            "@" + uninit_scalar.ident);
                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(uninit_scalar.ident, global_alloc);
                    },
                    [&](const VarDefAST::InitializedScalar& init_scalar) {
                        // eval init value
                        // init value must be constexpr
                        const auto& init_val = expect_node<InitValAST>(*init_scalar.init_val, "InitValAST");
                        const auto& expr_init = std::get<InitValAST::ExprInit>(init_val.payload);
                        const auto& exp_ast = expect_node<ExpAST>(*expr_init.exp, "ExpAST");

                        auto init_value = get_or_create_constant(eval_exp(exp_ast), module);

                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            init_value,
                            i32_ptr_type,
                            "@" + init_scalar.ident);
                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(init_scalar.ident, global_alloc);
                    },
                    [&](const VarDefAST::UninitializedArray& uninit_array) {
                        // get array size
                        if (uninit_array.const_dims.empty()) {
                            throw std::runtime_error("array must have at least one dimension: "
                                                     + uninit_array.ident);
                        }

                        std::vector<int32_t> array_dims;
                        for (const auto& item : uninit_array.const_dims) {
                            const auto& exp_ast = expect_node<ExpAST>(*item, "ExpAST");
                            array_dims.push_back(eval_exp(exp_ast));
                        }

                        int32_t length = std::accumulate(array_dims.begin(),
                                                         array_dims.end(),
                                                         1,
                                                         std::multiplies<int32_t>());
                        if (length <= 0) {
                            throw std::runtime_error("array length must be positive: " + uninit_array.ident);
                        }

                        // set array type
                        auto* array_type = get_array_type(array_dims, i32_type);

                        // alloc global array
                        auto* zero_init = module.make_value<IRZeroInit>(array_type);
                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            zero_init,
                            get_pointer_type(array_type),
                            "@" + uninit_array.ident);
                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(uninit_array.ident, global_alloc);
                    },
                    [&](const VarDefAST::InitializedArray& init_array) {
                        if (init_array.const_dims.empty()) {
                            throw std::runtime_error("array must have at least one dimension: "
                                                     + init_array.ident);
                        }

                        const auto& init_val =
                            expect_node<InitValAST>(*init_array.init_val, "InitValAST");
                        if (std::holds_alternative<InitValAST::ExprInit>(init_val.payload)) {
                            throw std::runtime_error(init_array.ident + "is array, init must be array");
                        }

                        std::vector<int32_t> array_dims;
                        array_dims.reserve(init_array.const_dims.size());
                        for (const auto& item : init_array.const_dims) {
                            const auto& size_exp = expect_node<ExpAST>(*item, "ExpAST");
                            array_dims.push_back(eval_exp(size_exp));
                        }

                        const int32_t length = std::accumulate(
                            array_dims.begin(),
                            array_dims.end(),
                            1,
                            std::multiplies<int32_t>());
                        if (length <= 0) {
                            throw std::runtime_error("array length must be positive : " + init_array.ident);
                        }

                        std::vector<int32_t> target_buffer;
                        target_buffer.reserve(length);
                        flatten_global_array_initializer(init_val, array_dims, target_buffer);

                        auto* array_type = get_array_type(array_dims, get_i32_type());
                        size_t cursor = 0;
                        auto* init_aggregate = build_array_aggregate_initializer(
                            array_type,
                            target_buffer,
                            cursor,
                            module);

                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            init_aggregate,
                            get_pointer_type(array_type),
                            "@" + init_array.ident);

                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(init_array.ident, global_alloc);
                    }},
                def.payload);
        }
        return;
    }

    throw std::runtime_error("unsupported global decl type");
}

IRFunction* RewindIRBuilder::lower_func_def(const FuncDefAST& ast, IRModule& module)
{
    auto* func = lookup_function(ast.ident);
    if (func == nullptr) {
        throw std::runtime_error("undefined function in lowering: " + ast.ident);
    }

    // set function context
    FuncContext ctx(module, *func);
    set_current_ctx(&ctx);

    // Initialize entry block
    // magic string "%entry"
    auto& basic_block = ctx.create_function_block("entry");
    ctx.set_current_block(basic_block);

    auto func_scope = ctx.make_scope();

    // alloc formal args
    for (size_t i = 0; i < ast.func_f_params.size(); ++i) {
        const auto& func_f_param =
            expect_node<FuncFParamAST>(*ast.func_f_params[i], "FuncFParam");

        // alloc variable
        auto& alloc = ctx.create_block_value<IRAllocInst>(
            get_pointer_type(func->params_[i]->type_),
            ctx.next_percent_name());

        // add to symbol table
        ctx.symbols().define_var(func_f_param.ident, &alloc);

        // store arg variable
        static_cast<void>(ctx.create_block_value<IRStoreInst>(
            func->params_[i],
            &alloc,
            get_unit_type()));
    }

    // lower BlockAST
    const auto& block = expect_node<BlockAST>(*ast.block, "BlockAST");
    lower_block(block);

    /*
     * ensure function have return
     */
    if (ctx.has_current_block()) {
        if (func->type_->return_type->is_unit()) {
            static_cast<void>(ctx.terminate_with_return(nullptr));
        } else if (func->type_->return_type->is_int32()) {
            static_cast<void>(ctx.terminate_with_return(
                get_or_create_constant(0, module)));
        } else {
            throw std::runtime_error("unsupported function return type");
        }
    }

    set_current_ctx(nullptr);
    return func;
}

const IRType* RewindIRBuilder::lower_func_f_params(const FuncFParamAST& ast)
{
    if (auto array_value = std::get_if<FuncFParamAST::Array>(&ast.payload)) {
        const IRType* base_type = get_i32_type();

        if (array_value->array_dim_size.size() == 0) {
            return get_pointer_type(base_type);
        }

        std::vector<int32_t> dims;
        dims.reserve(array_value->array_dim_size.size());
        for (const auto& item : array_value->array_dim_size) {
            const auto& exp_ast = expect_node<ExpAST>(*item, "ExpAST");
            dims.push_back(eval_exp(exp_ast));
        }

        const IRType* array_type = get_array_type(dims, base_type);
        return get_pointer_type(array_type);
    } else {
        return get_i32_type();
    }
}

void RewindIRBuilder::lower_block(const BlockAST& ast)
{
    auto scope = current_ctx_->make_scope();

    for (const auto& item : ast.items) {
        if (!current_ctx_->has_current_block()) {
            break;
        }

        if (auto* stmt = dynamic_cast<StmtAST*>(item.get())) {
            lower_stmt(*stmt);
        }
        // lower variable and const
        if (auto* decl = dynamic_cast<DeclAST*>(item.get())) {
            if (auto* const_decl = dynamic_cast<ConstDeclAST*>(decl->const_or_var.get())) {
                lower_const_decl(*const_decl);
            } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(decl->const_or_var.get())) {
                lower_var_decl(*var_decl);
            } else {
                throw std::runtime_error("unsupported decl type");
            }
        }
    }
}

/*
 * lower_const, not create IRConstant,
 * just define_const into symbol table
 * only eval_exp use constant , then create constant
 */
void RewindIRBuilder::lower_const_decl(const ConstDeclAST& ast)
{
    for (const auto& def_base : ast.const_defs) {
        const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
        std::visit(
            overloaded{
                [&](const ConstDefAST::ConstExpr& const_expr) {
                    const auto& init =
                        expect_node<ConstInitValAST>(*const_expr.const_init_val, "ConstInitValAST");
                    const auto& expr_init =
                        std::get<ConstInitValAST::ConstExprInit>(init.payload);
                    const auto& exp = expect_node<ExpAST>(*expr_init.const_exp, "ExpAST");
                    int32_t value = eval_exp(exp);
                    current_ctx_->symbols().define_const(const_expr.ident, value);
                },
                [&](const ConstDefAST::ConstArray& const_array) {
                    // get array dims
                    if (const_array.const_dims.empty()) {
                        throw std::runtime_error("array must have at least one dimension: "
                                                 + const_array.ident);
                    }
                    std::vector<int32_t> array_dims;
                    array_dims.reserve(const_array.const_dims.size());
                    for (const auto& item : const_array.const_dims) {
                        const auto& size_exp = expect_node<ExpAST>(*item, "ExpAST");
                        array_dims.push_back(eval_exp(size_exp));
                    }

                    int32_t length = std::accumulate(
                        array_dims.begin(),
                        array_dims.end(),
                        1,
                        std::multiplies<int32_t>());

                    if (length <= 0) {
                        throw std::runtime_error("array length must be positive : " + const_array.ident);
                    }

                    auto* array_type = get_array_type(array_dims, get_i32_type());
                    auto& alloc = current_ctx_->create_block_value<IRAllocInst>(
                        get_pointer_type(array_type),
                        current_ctx_->next_at_name(const_array.ident));
                    current_ctx_->symbols().define_var(const_array.ident, &alloc, true);

                    // get array init
                    const auto& init_val =
                        expect_node<ConstInitValAST>(*const_array.const_init_val, "ConstInitValAST");
                    if (std::holds_alternative<ConstInitValAST::ConstExprInit>(init_val.payload)) {
                        throw std::runtime_error(const_array.ident + "is array, init must be array");
                    }

                    // flatten nested array initialization into a linear buffer
                    std::vector<int32_t> target_buffer;
                    target_buffer.reserve(length);
                    flatten_const_array_initializer(init_val, array_dims, target_buffer);

                    size_t cursor = 0;
                    auto* init_aggregate = build_array_aggregate_initializer(
                        array_type,
                        target_buffer,
                        cursor,
                        current_ctx_->module());

                    static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                        init_aggregate,
                        &alloc,
                        get_unit_type()));
                }},
            def.payload);
    }
}

void RewindIRBuilder::lower_var_decl(const VarDeclAST& ast)
{
    auto* i32_ptr_type = get_pointer_type(get_i32_type());

    for (const auto& def_base : ast.var_defs) {
        const auto& def = expect_node<VarDefAST>(*def_base, "VarDefAST");
        std::visit(
            overloaded{
                // int x;
                [&](const VarDefAST::UninitializedScalar& uninit_scalar) {
                    auto& alloc = current_ctx_->create_block_value<IRAllocInst>(
                        i32_ptr_type,
                        current_ctx_->next_at_name(uninit_scalar.ident));
                    current_ctx_->symbols().define_var(uninit_scalar.ident, &alloc);
                },
                // int x = 10;
                [&](const VarDefAST::InitializedScalar& init_scalar) {
                    // alloc variable
                    auto& alloc = current_ctx_->create_block_value<IRAllocInst>(
                        i32_ptr_type,
                        current_ctx_->next_at_name(init_scalar.ident));
                    current_ctx_->symbols().define_var(init_scalar.ident, &alloc);

                    // get init
                    const auto& init_val = expect_node<InitValAST>(*init_scalar.init_val, "InitValAST");
                    const auto& expr_init = std::get<InitValAST::ExprInit>(init_val.payload);
                    const auto& exp_ast = expect_node<ExpAST>(*expr_init.exp, "ExpAST");

                    auto init_value = lower_exp(exp_ast);

                    static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                        init_value, &alloc, get_unit_type()));
                },
                [&](const VarDefAST::UninitializedArray& uninit_array) {
                    // get array dims
                    if (uninit_array.const_dims.empty()) {
                        throw std::runtime_error("array must have at least one dimension: "
                                                 + uninit_array.ident);
                    }
                    std::vector<int32_t> array_dims;
                    array_dims.reserve(uninit_array.const_dims.size());
                    for (const auto& item : uninit_array.const_dims) {
                        const auto& size_exp = expect_node<ExpAST>(*item, "ExpAST");
                        array_dims.push_back(eval_exp(size_exp));
                    }

                    // get array length
                    int32_t length = std::accumulate(array_dims.begin(), array_dims.end(), 1, std::multiplies<int32_t>());
                    if (length <= 0) {
                        throw std::runtime_error("array length must be positive: " + uninit_array.ident);
                    }

                    // get array type
                    const auto* array_type = get_array_type(array_dims, get_i32_type());
                    auto& alloc = current_ctx_->create_block_value<IRAllocInst>(
                        get_pointer_type(array_type),
                        current_ctx_->next_at_name(uninit_array.ident));
                    current_ctx_->symbols().define_var(uninit_array.ident, &alloc);
                },
                [&](const VarDefAST::InitializedArray& init_array) {
                    // get array dims
                    if (init_array.const_dims.empty()) {
                        throw std::runtime_error("array must have at least one dimension: "
                                                 + init_array.ident);
                    }

                    std::vector<int32_t> array_dims;
                    array_dims.reserve(init_array.const_dims.size());
                    for (const auto& item : init_array.const_dims) {
                        const auto& size_exp = expect_node<ExpAST>(*item, "ExpAST");
                        array_dims.push_back(eval_exp(size_exp));
                    }

                    // get array length
                    int32_t length = std::accumulate(array_dims.begin(), array_dims.end(), 1, std::multiplies<int32_t>());
                    if (length <= 0) {
                        throw std::runtime_error("array length must be positive" + init_array.ident);
                    }

                    auto* array_type = get_array_type(array_dims, get_i32_type());
                    auto& alloc = current_ctx_->create_block_value<IRAllocInst>(
                        get_pointer_type(array_type),
                        current_ctx_->next_at_name(init_array.ident));

                    // store local array to symbol table
                    current_ctx_->symbols().define_var(init_array.ident, &alloc);

                    // local array init
                    const auto& array_init = expect_node<InitValAST>(*init_array.init_val, "InitValAST");
                    if (std::holds_alternative<InitValAST::ExprInit>(array_init.payload)) {
                        throw std::runtime_error(init_array.ident + "is array, init must be array");
                    }

                    std::vector<IRValue*> target_buffer;
                    target_buffer.reserve(length);
                    flatten_local_runtime_array_initializer(array_init, array_dims, target_buffer);
                    emit_local_array_initializer_stores(&alloc, array_dims, target_buffer);
                },
            },
            def.payload);
    }
}

IRValue* RewindIRBuilder::get_or_create_constant(int32_t value, IRModule& module)
{
    auto it = constant_cache_.find(value);
    if (it != constant_cache_.end()) {
        return it->second;
    }
    auto* c = module.make_value<IRConstant>(value, get_i32_type());
    constant_cache_[value] = c;
    return c;
}

// check if current_ctx_ exist
// first local lookup, then global lookup
std::optional<SymbolTable::LookupResult>
RewindIRBuilder::lookup_value(const std::string& name) const
{
    if (current_ctx_ != nullptr) {
        if (auto local = current_ctx_->symbols().lookup_value(name)) {
            return local;
        }
    }
    return module_symbols_.lookup_value(name);
}

IRFunction* RewindIRBuilder::lookup_function(const std::string& name) const
{
    return module_symbols_.lookup_function(name);
}

void RewindIRBuilder::set_current_ctx(FuncContext* ctx)
{
    current_ctx_ = ctx;
}
} // namespace rewind_ir
