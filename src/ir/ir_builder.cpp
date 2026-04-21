#include "ir_builder.h"
#include "ast.h"
#include "func_context.h"
#include "rewind_ir.h"
#include "ir_type.h"
#include "symbol_table.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <type_traits>
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

inline const IRType* get_array_storage_type(const IRValue* storage)
{
    if (!storage->type_->is_pointer()) {
        return nullptr;
    }

    const auto* pointer_type = storage->type_->as<IRPointerType>();
    if (pointer_type->base_type->is_array() || pointer_type->base_type->is_pointer()) {
        return pointer_type->base_type;
    }

    return nullptr;
}

inline bool is_array_storage(const IRValue* storage)
{
    return get_array_storage_type(storage) != nullptr;
}

inline bool is_call_arg_type_compatible(const IRType* actual, const IRType* expected)
{
    if (actual == expected) {
        return true;
    }

    if (actual == nullptr || expected == nullptr) {
        return false;
    }

    if (!actual->is_pointer() || !expected->is_pointer()) {
        return false;
    }

    const auto* actual_base = actual->as<IRPointerType>()->base_type;
    if (!actual_base->is_array()) {
        return false;
    }

    const auto* decayed_type = get_pointer_type(actual_base->as<IRArrayType>()->element_type);
    return decayed_type == expected;
}

const LValAST* try_extract_lvalue(const BaseAST& node)
{
    if (const auto* lval = dynamic_cast<const LValAST*>(&node)) {
        return lval;
    }

    if (const auto* exp = dynamic_cast<const ExpAST*>(&node)) {
        if (!exp->lor_exp) {
            return nullptr;
        }
        return try_extract_lvalue(*exp->lor_exp);
    }

    if (const auto* lor = dynamic_cast<const LOrExpAST*>(&node)) {
        if (const auto* simple = std::get_if<LOrExpAST::Simple>(&lor->payload)) {
            return try_extract_lvalue(*simple->land_exp);
        }
        return nullptr;
    }

    if (const auto* land = dynamic_cast<const LAndExpAST*>(&node)) {
        if (const auto* simple = std::get_if<LAndExpAST::Simple>(&land->payload)) {
            return try_extract_lvalue(*simple->eq_exp);
        }
        return nullptr;
    }

    if (const auto* eq = dynamic_cast<const EqExpAST*>(&node)) {
        if (const auto* simple = std::get_if<EqExpAST::Simple>(&eq->payload)) {
            return try_extract_lvalue(*simple->rel_exp);
        }
        return nullptr;
    }

    if (const auto* rel = dynamic_cast<const RelExpAST*>(&node)) {
        if (const auto* simple = std::get_if<RelExpAST::Simple>(&rel->payload)) {
            return try_extract_lvalue(*simple->add_exp);
        }
        return nullptr;
    }

    if (const auto* add = dynamic_cast<const AddExpAST*>(&node)) {
        if (const auto* simple = std::get_if<AddExpAST::Simple>(&add->payload)) {
            return try_extract_lvalue(*simple->mul_exp);
        }
        return nullptr;
    }

    if (const auto* mul = dynamic_cast<const MulExpAST*>(&node)) {
        if (const auto* simple = std::get_if<MulExpAST::Simple>(&mul->payload)) {
            return try_extract_lvalue(*simple->unary_exp);
        }
        return nullptr;
    }

    if (const auto* unary = dynamic_cast<const UnaryExpAST*>(&node)) {
        if (const auto* primary = std::get_if<UnaryExpAST::Primary>(&unary->payload)) {
            return try_extract_lvalue(*primary->exp);
        }
        return nullptr;
    }

    if (const auto* primary = dynamic_cast<const PrimaryExpAST*>(&node)) {
        if (const auto* lvalue = std::get_if<PrimaryExpAST::LValue>(&primary->payload)) {
            return dynamic_cast<const LValAST*>(lvalue->lval.get());
        }
        if (const auto* expr = std::get_if<PrimaryExpAST::Expression>(&primary->payload)) {
            return try_extract_lvalue(*expr->exp);
        }
    }

    return nullptr;
}

template <typename T>
const T& expect_node(const BaseAST& node, const char* expected_name)
{
    const auto* p = dynamic_cast<const T*>(&node);
    if (p == nullptr) {
        throw std::runtime_error(std::string("AST type mismatch, expected: ") + expected_name);
    }
    return *p;
}

inline IRBinaryOp ast_op_to_ir_op(BinaryOp op)
{
    switch (op) {
    case BinaryOp::MUL:
        return IRBinaryOp::MUL;
    case BinaryOp::DIV:
        return IRBinaryOp::DIV;
    case BinaryOp::MOD:
        return IRBinaryOp::MOD;
    case BinaryOp::ADD:
        return IRBinaryOp::ADD;
    case BinaryOp::SUB:
        return IRBinaryOp::SUB;
    case BinaryOp::LAND:
        return IRBinaryOp::AND;
    case BinaryOp::LOR:
        return IRBinaryOp::OR;
    case BinaryOp::EQ:
        return IRBinaryOp::EQ;
    case BinaryOp::NEQ:
        return IRBinaryOp::NEQ;
    case BinaryOp::LT:
        return IRBinaryOp::LT;
    case BinaryOp::GT:
        return IRBinaryOp::GT;
    case BinaryOp::LE:
        return IRBinaryOp::LE;
    case BinaryOp::GE:
        return IRBinaryOp::GE;
    }
    throw std::runtime_error("unsupported BinaryOp kind");
}

inline int32_t eval_binary_op(BinaryOp op, int32_t a, int32_t b)
{
    switch (op) {
    case BinaryOp::MUL:
        return a * b;
    case BinaryOp::DIV:
        return a / b;
    case BinaryOp::MOD:
        return a % b;
    case BinaryOp::ADD:
        return a + b;
    case BinaryOp::SUB:
        return a - b;
    case BinaryOp::LT:
        return a < b;
    case BinaryOp::GT:
        return a > b;
    case BinaryOp::LE:
        return a <= b;
    case BinaryOp::GE:
        return a >= b;
    case BinaryOp::EQ:
        return a == b;
    case BinaryOp::NEQ:
        return a != b;
    default:
        break;
    }
    throw std::runtime_error("unsupported BinaryOp for const eval");
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

IRFunction* declare_external_function(IRModule& module,
                                      SymbolTable& module_symbols,
                                      const std::string& name,
                                      std::vector<const IRType*> param_types,
                                      const IRType* return_type)
{
    auto* function_type = get_function_type(param_types, return_type);
    auto* func = module.make_function(function_type, name, true);
    module_symbols.define_function(name, func);

    for (size_t i = 0; i < param_types.size(); ++i) {
        auto* arg_ref = module.make_value<IRFuncArgRef>(i, param_types[i]);
        module.append_param(*func, *arg_ref);
    }

    return func;
}

} // namespace

// Overloaded struct defined for use with std::variant
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

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

void RewindIRBuilder::declare_library_function(IRModule& module)
{
    const auto* i32 = get_i32_type();
    const auto* unit = get_unit_type();
    const auto* i32_ptr = get_pointer_type(i32);

    declare_external_function(module, module_symbols_, "getint", {}, i32);
    declare_external_function(module, module_symbols_, "getch", {}, i32);
    declare_external_function(module, module_symbols_, "putint", {i32}, unit);
    declare_external_function(module, module_symbols_, "putch", {i32}, unit);
    declare_external_function(module, module_symbols_, "starttime", {}, unit);
    declare_external_function(module, module_symbols_, "stoptime", {}, unit);

    // Keep the standard SysY array runtime declarations available for later
    // array support.
    declare_external_function(module, module_symbols_, "getarray", {i32_ptr}, i32);
    declare_external_function(module, module_symbols_, "putarray", {i32, i32_ptr}, unit);
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
                        process_const_array_init(init_val, array_dims, target_buffer);

                        auto* array_type = get_array_type(array_dims, get_i32_type());
                        size_t cursor = 0;
                        auto* init_aggregate = build_array_aggregate_from_flat(
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

                        int32_t length = std::accumulate(array_dims.begin(), array_dims.end(),
                                                         1, std::multiplies<int32_t>());
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
                        process_array_init_const(init_val, array_dims, target_buffer);

                        auto* array_type = get_array_type(array_dims, get_i32_type());
                        size_t cursor = 0;
                        auto* init_aggregate = build_array_aggregate_from_flat(
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

                    int32_t length = std::accumulate(array_dims.begin(), array_dims.end(),
                                                     1, std::multiplies<int32_t>());
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
                    process_const_array_init(init_val, array_dims, target_buffer);

                    size_t cursor = 0;
                    auto* init_aggregate = build_array_aggregate_from_flat(
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

                    auto* zero_init = current_ctx_->module().make_value<IRZeroInit>(array_type);
                    static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                        zero_init,
                        &alloc,
                        get_unit_type()));
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
                    process_array_init_runtime(array_init, array_dims, target_buffer);
                    local_array_init(&alloc, array_dims, target_buffer);
                },
            },
            def.payload);
    }
}

void RewindIRBuilder::lower_stmt(const StmtAST& ast)
{
    std::visit(
        overloaded{
            [&](const StmtAST::Return& ret_stmt) {
                IRValue* ret_value = nullptr;

                /*
                 * check if return exp exist
                 * then check if return exp type same as  function return type
                 */
                if (ret_stmt.exp) {
                    if (current_ctx_->current_function().type_->return_type->is_unit()) {
                        throw std::runtime_error("void function should not return a value");
                    }
                    const auto& exp = expect_node<ExpAST>(*ret_stmt.exp, "ExpAST");
                    ret_value = lower_exp(exp);
                } else if (current_ctx_->current_function().type_->return_type->is_int32()) {
                    ret_value = get_or_create_constant(0, current_ctx_->module());
                }

                static_cast<void>(current_ctx_->terminate_with_return(ret_value));
            },
            [&](const StmtAST::Assign& assign_stmt) {
                // assign

                // store exp_value, alloc
                const auto& exp = expect_node<ExpAST>(*assign_stmt.exp, "ExpAST");
                auto exp_value = lower_exp(exp);

                const auto& lval_ast = expect_node<LValAST>(*assign_stmt.lval, "LValAST");

                auto* var = lower_lval_address(lval_ast);

                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    exp_value,
                    var,
                    get_unit_type()));
            },
            [&](const StmtAST::Block& block_stmt) {
                const auto& block = expect_node<BlockAST>(*block_stmt.block, "BlockAST");
                lower_block(block);
            },
            [&](const StmtAST::Exp& exp_stmt) {
                // exp is empty, just return
                if (!exp_stmt.exp) {
                    return;
                }
                const auto& exp = expect_node<ExpAST>(*exp_stmt.exp, "ExpAST");
                static_cast<void>(lower_exp(exp));
            },
            [&](const StmtAST::SelectStmt& select_stmt) {
                // if ( cond ) if_stmt else else_stmt , else_stmt may be empty
                const auto& exp = expect_node<ExpAST>(*select_stmt.exp, "ExpAST");
                const auto& if_stmt = expect_node<StmtAST>(*select_stmt.if_stmt, "StmtAST");
                const StmtAST* else_stmt = select_stmt.else_stmt ? &expect_node<StmtAST>(*select_stmt.else_stmt, "StmtAST") : nullptr;

                // condition value
                auto* cond = lower_exp(exp);

                // def if_basic_block, else_basic_block  merge_basic_block
                auto& then_bb = current_ctx_->create_function_block("then");
                IRBasicBlock* else_bb = nullptr;
                if (else_stmt != nullptr) {
                    else_bb = &current_ctx_->create_function_block("else");
                }

                IRBasicBlock* merge_bb = nullptr;
                if (else_stmt == nullptr) {
                    merge_bb = &current_ctx_->create_function_block("end");
                }

                // current_block add branch inst
                static_cast<void>(current_ctx_->terminate_with_branch(
                    cond,
                    then_bb,
                    *(else_stmt != nullptr ? else_bb : merge_bb)));

                // switch then_bb
                current_ctx_->set_current_block(then_bb);
                lower_stmt(if_stmt);
                IRBasicBlock* then_fallthrough = current_ctx_->current_block_or_null();

                // check if then_bb terminated
                // consider example : if ( exp ) return exp;  don't need jump inst
                if (then_fallthrough != nullptr) {
                    if (merge_bb == nullptr) {
                        merge_bb = &current_ctx_->create_function_block("end");
                    }
                    current_ctx_->set_current_block(*then_fallthrough);
                    static_cast<void>(current_ctx_->terminate_with_jump(*merge_bb));
                }

                // switch else_basic_block
                if (else_stmt != nullptr) {
                    current_ctx_->set_current_block(*else_bb);
                    lower_stmt(*else_stmt);
                    IRBasicBlock* else_fallthrough = current_ctx_->current_block_or_null();

                    // check if else_basic_block terminated
                    if (else_fallthrough != nullptr) {
                        if (merge_bb == nullptr) {
                            merge_bb = &current_ctx_->create_function_block("end");
                        }
                        current_ctx_->set_current_block(*else_fallthrough);
                        static_cast<void>(current_ctx_->terminate_with_jump(*merge_bb));
                    }
                }

                // if_bb and else_bb all terminate, don't need merge_bb
                // this way can prevent emtpy merge_bb
                if (merge_bb != nullptr) {
                    current_ctx_->set_current_block(*merge_bb);
                } else {
                    current_ctx_->clear_current_block();
                }
            },
            [&](const StmtAST::LoopStmt& loop_stmt) {
                const auto& exp = expect_node<ExpAST>(*loop_stmt.exp, "ExpAST");
                const auto& body_stmt = expect_node<StmtAST>(*loop_stmt.body_stmt, "StmtAST");

                auto& while_entry = current_ctx_->create_function_block("while_entry");
                auto& while_body = current_ctx_->create_function_block("while_body");
                auto& end = current_ctx_->create_function_block("end");

                // record break and continue basic block
                current_ctx_->push_loop(end, while_entry);

                // preheader -> while_entry
                static_cast<void>(current_ctx_->terminate_with_jump(while_entry));

                // while_entry:
                //   evaluate condition
                //   br cond, while_body, end
                current_ctx_->set_current_block(while_entry);
                auto* cond = lower_exp(exp);
                static_cast<void>(current_ctx_->terminate_with_branch(
                    cond,
                    while_body,
                    end));

                // while_body:
                //   lower body
                //   if body still falls through, jump back to while_entry
                current_ctx_->set_current_block(while_body);
                lower_stmt(body_stmt);

                // check if while_body is terminated
                auto* body_fallthrough = current_ctx_->current_block_or_null();
                if (body_fallthrough != nullptr) {
                    current_ctx_->set_current_block(*body_fallthrough);
                    static_cast<void>(current_ctx_->terminate_with_jump(while_entry));
                }

                // exit loop
                current_ctx_->pop_loop();

                // end:
                //   subsequent statements continue here
                current_ctx_->set_current_block(end);
            },
            [&](const StmtAST::LoopControlStmt& control_stmt) {
                if (!current_ctx_->in_loop()) {
                    if (control_stmt.kind == StmtAST::LoopControlStmt::Kind::Break) {
                        throw std::runtime_error("break used outside while");
                    } else {
                        throw std::runtime_error("continue used outside while");
                    }
                }

                switch (control_stmt.kind) {
                case StmtAST::LoopControlStmt::Kind::Break: {
                    current_ctx_->terminate_with_jump(*current_ctx_->current_loop().break_target);
                    break;
                }
                case StmtAST::LoopControlStmt::Kind::Continue: {
                    current_ctx_->terminate_with_jump(*current_ctx_->current_loop().continue_target);
                    break;
                }
                }
            },
            [&](const auto& other) {
                std::string type_name = typeid(other).name();
                throw std::runtime_error("Unsupported statement type: " + type_name);
            }},
        ast.payload);
}

/*
 * lower_*exp not only return IRValue* also may advance the current insertion point
 */
IRValue* RewindIRBuilder::lower_exp(const ExpAST& ast)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return lower_lor_exp(lor_exp);
}

// Short-circuit evaluation
/*
 * current basic block
 * lower lhs
 * alloc result
 * lhs_bool : lhs != 0
 * br lhs_bool short_true rhs_bb
 *
 * short_true basic block
 * store 1 result
 * jump merge
 *
 * rhs_bb basic block
 * store rhs_value result
 * jump merge
 *
 * merge basic block
 * % = load result
 */
IRValue* RewindIRBuilder::lower_lor_exp(const LOrExpAST& ast)
{
    auto& module = current_ctx_->module();
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& simple) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                return lower_land_exp(land_exp);
            },
            [&](const LOrExpAST::Binary& binary) -> IRValue* {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");

                auto& short_true = current_ctx_->create_function_block("short_true");
                auto& rhs_bb = current_ctx_->create_function_block("rhs_basic_block");
                auto& merge = current_ctx_->create_function_block("merge");

                // lower lhs
                auto* lhs = lower_lor_exp(lor_exp);

                // @result = alloc i32
                auto& result_slot = current_ctx_->create_block_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    current_ctx_->next_at_name("lor_tmp"));

                // lhs_bool : lhs != 0
                auto* zero = get_or_create_constant(0, module);
                auto& lhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());

                // br lhs_bool short_true rhs_bb
                static_cast<void>(current_ctx_->terminate_with_branch(
                    &lhs_bool,
                    short_true,
                    rhs_bb));

                // * short_true basic block
                current_ctx_->set_current_block(short_true);

                // store 1 result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    get_or_create_constant(1, module),
                    &result_slot,
                    get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // * rhs_bb basic block
                current_ctx_->set_current_block(rhs_bb);
                // lower rhs
                auto* rhs = lower_land_exp(land_exp);
                auto& rhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());

                // store rhs_bool, result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    &rhs_bool, &result_slot, get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // merge basic block
                current_ctx_->set_current_block(merge);
                return &current_ctx_->create_block_value<IRLoadInst>(
                    &result_slot,
                    get_i32_type(),
                    current_ctx_->next_percent_name());
            }},
        ast.payload);
}

// a && b : evaluate rhs only when lhs is non-zero
IRValue* RewindIRBuilder::lower_land_exp(const LAndExpAST& ast)
{
    auto& module = current_ctx_->module();
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& simple) -> IRValue* {
                const auto& eq_exp = expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                return lower_eq_exp(eq_exp);
            },
            [&](const LAndExpAST::Binary& binary) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                const auto& eq_exp = expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");

                // eval lhs
                auto lhs = lower_land_exp(land_exp);

                auto& result_slot = current_ctx_->create_block_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    current_ctx_->next_at_name("land_tmp"));

                // initialization basic_block
                auto& short_false = current_ctx_->create_function_block("short_false");
                auto& rhs_bb = current_ctx_->create_function_block("rhs_basic_block");
                auto& merge = current_ctx_->create_function_block("merge");

                auto* zero = get_or_create_constant(0, module);
                auto& lhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());
                static_cast<void>(current_ctx_->terminate_with_branch(
                    &lhs_bool,
                    rhs_bb,
                    short_false));

                // * short_false basic block
                current_ctx_->set_current_block(short_false);
                auto* zero_value = get_or_create_constant(0, module);
                // store 0 result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    zero_value, &result_slot, get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // * rhs_bb basic block
                current_ctx_->set_current_block(rhs_bb);
                // eval rhs
                auto* rhs = lower_eq_exp(eq_exp);
                auto& rhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());

                // store rhs_bool, result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    &rhs_bool, &result_slot, get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // merge basic block
                // % = load @result
                current_ctx_->set_current_block(merge);
                return &current_ctx_->create_block_value<IRLoadInst>(
                    &result_slot,
                    get_i32_type(),
                    current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_eq_exp(const EqExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& simple) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                return lower_rel_exp(rel_exp);
            },
            [&](const EqExpAST::Binary& binary) -> IRValue* {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");

                auto lhs = lower_eq_exp(eq_exp);
                auto rhs = lower_rel_exp(rel_exp);
                auto op = ast_op_to_ir_op(binary.op);
                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_rel_exp(const RelExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& simple) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*simple.add_exp, "AddExpAST");
                return lower_add_exp(add_exp);
            },
            [&](const RelExpAST::Binary& binary) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*binary.add_exp, "AddExpAST");

                auto lhs = lower_rel_exp(rel_exp);
                auto rhs = lower_add_exp(add_exp);
                auto op = ast_op_to_ir_op(binary.op);
                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_add_exp(const AddExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& s) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                return lower_mul_exp(mul_exp);
            },
            [&](const AddExpAST::Binary& b) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");

                auto lhs = lower_add_exp(add_exp);
                auto rhs = lower_mul_exp(mul_exp);
                auto op = ast_op_to_ir_op(b.op);

                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_mul_exp(const MulExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& s) -> IRValue* {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                return lower_unary_exp(unary_exp);
            },
            [&](const MulExpAST::Binary& b) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");

                auto lhs = lower_mul_exp(mul_exp);
                auto rhs = lower_unary_exp(unary_exp);
                auto op = ast_op_to_ir_op(b.op);
                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_unary_exp(const UnaryExpAST& ast)
{
    auto& module = current_ctx_->module();
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& unary) -> IRValue* {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*unary.exp, "PrimaryExpAST");
                return lower_primary_exp(primary);
            },
            [&](const UnaryExpAST::Unary& unary) -> IRValue* {
                const auto& unary_exp = expect_node<UnaryExpAST>(*unary.exp, "UnaryExpAST");
                auto operand = lower_unary_exp(unary_exp);

                //
                auto zero = get_or_create_constant(0, module);
                switch (unary.op) {
                case UnaryOp::PLUS:
                    return operand; // +x = x
                case UnaryOp::MINUS: {
                    return &current_ctx_->create_block_value<IRBinaryInst>(
                        IRBinaryOp::SUB, zero, operand, get_i32_type(),
                        current_ctx_->next_percent_name());
                }
                case UnaryOp::NOT: {
                    return &current_ctx_->create_block_value<IRBinaryInst>(
                        IRBinaryOp::EQ, operand, zero, get_i32_type(),
                        current_ctx_->next_percent_name());
                }
                }
                throw std::runtime_error("invalid UnaryOp");
            },
            [&](const UnaryExpAST::FuncCall& funcCall) -> IRValue* {
                // check if function exist
                auto* callee = lookup_function(funcCall.ident);

                if (callee == nullptr) {
                    throw std::runtime_error("undefined function: " + funcCall.ident);
                }

                // get params
                std::vector<IRValue*> args;
                if (funcCall.func_r_params != nullptr) {
                    const auto& func_r_params =
                        expect_node<FuncRParamsAST>(*funcCall.func_r_params, "FuncRParamsAST");

                    if (func_r_params.exps.size() != callee->type_->params.size()) {
                        throw std::runtime_error(
                            "function argument count mismatch: " + funcCall.ident);
                    }

                    for (size_t i = 0; i < func_r_params.exps.size(); ++i) {
                        const auto& item = func_r_params.exps[i];
                        const auto& exp_ast = expect_node<ExpAST>(*item, "ExpAST");
                        args.push_back(lower_call_arg(exp_ast, callee->type_->params[i]));
                    }
                }

                // check if params match
                // from two sides : size and type_
                if (args.size() != callee->type_->params.size()) {
                    throw std::runtime_error(
                        "function argument count mismatch: " + funcCall.ident);
                }

                for (size_t i = 0; i < args.size(); ++i) {
                    if (!is_call_arg_type_compatible(args[i]->type_, callee->type_->params[i])) {
                        throw std::runtime_error(
                            "function argument type mismatch: " + funcCall.ident);
                    }
                }

                // create call inst
                if (callee->type_->return_type->is_unit()) {
                    return &current_ctx_->create_block_value<IRCallInst>(
                        callee,
                        std::move(args),
                        callee->type_->return_type);
                }

                return &current_ctx_->create_block_value<IRCallInst>(
                    callee,
                    std::move(args),
                    callee->type_->return_type,
                    current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_call_arg(const ExpAST& ast, const IRType* expected_ty)
{
    // scalar
    if (expected_ty->is_int32()) {
        return lower_exp(ast);
    }

    // pointer-like actual argument, mainly array decay
    if (expected_ty->is_pointer()) {
        const auto* lval_ast = try_extract_lvalue(ast);

        // not variable
        if (lval_ast == nullptr) {
            throw std::runtime_error("actual param type not match formal param");
        }

        // not found or constant
        auto lval = lookup_value(lval_ast->ident);
        if (!lval || !std::holds_alternative<SymbolTable::Var>(*lval)) {
            throw std::runtime_error("actual param type not match formal param");
        }

        // not array
        auto* storage = std::get<SymbolTable::Var>(*lval).alloc;
        if (!is_array_storage(storage)) {
            throw std::runtime_error("actual param type not match formal param");
        }

        IRValue* actual = nullptr;
        IRValue* zero_value = get_or_create_constant(0, current_ctx_->module());

        // two array type:
        // *[i32, ... ]
        // **i32
        if (lval_ast->indices.empty()) {
            const IRType* storage_type = get_array_storage_type(storage);
            if (storage_type->is_array()) {
                const IRType* elem_type = storage_type->as<IRArrayType>()->element_type;
                actual = &current_ctx_->create_block_value<IRGetElemPtrInst>(
                    storage,
                    zero_value,
                    get_pointer_type(elem_type),
                    current_ctx_->next_percent_name());
            } else if (storage_type->is_pointer()) {
                IRValue* loaded_ptr = &current_ctx_->create_block_value<IRLoadInst>(
                    storage,
                    storage_type,
                    current_ctx_->next_percent_name());
                actual = loaded_ptr;
            }
        } else {
            actual = lower_lval_address(*lval_ast, true);
        }

        if (actual == nullptr) {
            throw std::runtime_error("actual param type not match formal param");
        }

        if (actual->type_ == expected_ty) {
            return actual;
        }

        // ? don't know what its function
        if (actual->type_->is_pointer()) {
            const auto* actual_base = actual->type_->as<IRPointerType>()->base_type;
            if (actual_base->is_array()) {
                const auto* decayed_type =
                    get_pointer_type(actual_base->as<IRArrayType>()->element_type);

                if (decayed_type == expected_ty) {
                    return &current_ctx_->create_block_value<IRGetElemPtrInst>(
                        actual,
                        zero_value,
                        decayed_type,
                        current_ctx_->next_percent_name());
                }
            }
        }
    }
    throw std::runtime_error("lower_call_arg error");
}

IRValue* RewindIRBuilder::lower_primary_exp(const PrimaryExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& number) -> IRValue* {
                return get_or_create_constant(number.value, current_ctx_->module());
            },
            [&](const PrimaryExpAST::Expression& expression) -> IRValue* {
                const auto& exp = expect_node<ExpAST>(*expression.exp, "ExpAST");
                return lower_exp(exp);
            },
            [&](const PrimaryExpAST::LValue& lvalue) -> IRValue* {
                const auto& lval_ast = expect_node<LValAST>(*lvalue.lval, "LValAST");
                return lower_lval_rvalue(lval_ast);
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_lval_array_address(const LValAST& ast,
                                                   IRValue* current_ptr,
                                                   const IRType* current_elem_type,
                                                   bool allow_array_decay)
{
    size_t array_dim = current_elem_type->as<IRArrayType>()->getArrayDim();

    if (array_dim < ast.indices.size()) {
        throw std::runtime_error("too many indices for array: " + ast.ident);
    }

    if (array_dim > ast.indices.size() && !allow_array_decay) {
        if (ast.indices.empty()) {
            throw std::runtime_error("array is not assignable without index: " + ast.ident);
        }
        throw std::runtime_error("less indices for array: " + ast.ident);
    }

    // chain getelemptr for multi-dimensional array access
    for (size_t i = 0; i < ast.indices.size(); ++i) {
        const auto& exp_ast = expect_node<ExpAST>(*ast.indices[i], "ExpAST");
        auto* index = lower_exp(exp_ast);

        // determine pointer type for next level
        current_elem_type = current_elem_type->as<IRArrayType>()->element_type;

        auto& ptr = current_ctx_->create_block_value<IRGetElemPtrInst>(
            current_ptr,
            index,
            get_pointer_type(current_elem_type),
            current_ctx_->next_percent_name());
        current_ptr = &ptr;
    }

    return current_ptr;
}

IRValue* RewindIRBuilder::lower_lval_pointer_address(const LValAST& ast,
                                                     IRValue* current_ptr,
                                                     const IRType* current_elem_type,
                                                     bool allow_array_decay)
{
    IRValue* loaded_ptr = &current_ctx_->create_block_value<IRLoadInst>(
        current_ptr,
        current_elem_type,
        current_ctx_->next_percent_name());

    if (ast.indices.empty()) {
        if (!allow_array_decay) {
            throw std::runtime_error("array is not assignable without index: " + ast.ident);
        }
        return loaded_ptr;
    }

    const auto& first_index_ast = expect_node<ExpAST>(*ast.indices[0], "ExpAST");
    auto* first_index = lower_exp(first_index_ast);
    current_elem_type = current_elem_type->as<IRPointerType>()->base_type;

    current_ptr = &current_ctx_->create_block_value<IRGetPtrInst>(
        loaded_ptr,
        first_index,
        get_pointer_type(current_elem_type),
        current_ctx_->next_percent_name());

    size_t remaining_dim = current_elem_type->is_array() ? current_elem_type->as<IRArrayType>()->getArrayDim() : 0;
    size_t provided_remaining = ast.indices.size() - 1;

    if (provided_remaining > remaining_dim) {
        throw std::runtime_error("too many indices for array: " + ast.ident);
    }

    if (provided_remaining < remaining_dim && !allow_array_decay) {
        throw std::runtime_error("less indices for array: " + ast.ident);
    }

    for (int i = 1; i < ast.indices.size(); i++) {
        const auto& exp_ast = expect_node<ExpAST>(*ast.indices[i], "ExpAST");
        auto* index = lower_exp(exp_ast);

        current_elem_type = current_elem_type->as<IRArrayType>()->element_type;

        auto& ptr = current_ctx_->create_block_value<IRGetElemPtrInst>(
            current_ptr,
            index,
            get_pointer_type(current_elem_type),
            current_ctx_->next_percent_name());
        current_ptr = &ptr;
    }
    return current_ptr;
}

IRValue* RewindIRBuilder::lower_lval_rvalue(const LValAST& ast)
{
    auto lval = lookup_value(ast.ident);

    if (!lval) {
        throw std::runtime_error(ast.ident + " is not exist");
    }

    // constexpr
    if (std::holds_alternative<SymbolTable::Constant>(*lval)) {
        return get_or_create_constant(
            std::get<SymbolTable::Constant>(*lval).value,
            current_ctx_->module());
    }

    auto* var = std::get<SymbolTable::Var>(*lval).alloc;

    // array
    if (is_array_storage(var)) {
        if (ast.indices.empty()) {
            throw std::runtime_error("array value is not supported without index: " + ast.ident);
        }

        IRValue* current_ptr = var;
        const IRType* current_elem_type = get_array_storage_type(var);

        if (current_elem_type->is_array()) {
            current_ptr = lower_lval_array_address(ast, current_ptr, current_elem_type, false);
        } else if (current_elem_type->is_pointer()) {
            current_ptr = lower_lval_pointer_address(ast, current_ptr, current_elem_type, false);
        }

        return &current_ctx_->create_block_value<IRLoadInst>(
            current_ptr,
            get_i32_type(),
            current_ctx_->next_percent_name());
    }

    if (!ast.indices.empty()) {
        throw std::runtime_error(ast.ident + " is not array");
    }

    // scalar
    return &current_ctx_->create_block_value<IRLoadInst>(
        var,
        get_i32_type(),
        current_ctx_->next_percent_name());
}

IRValue* RewindIRBuilder::lower_lval_address(const LValAST& ast, bool allow_array_decay)
{
    // check if value can be modified
    auto lval = lookup_value(ast.ident);

    if (!lval) {
        throw std::runtime_error(ast.ident + " is not exist");
    }

    if (std::holds_alternative<SymbolTable::Constant>(*lval)) {
        throw std::runtime_error(ast.ident + " is constant");
    }

    const auto& var_info = std::get<SymbolTable::Var>(*lval);
    if (var_info.is_const) {
        throw std::runtime_error(ast.ident + " is const");
    }

    auto* var = var_info.alloc;

    // array
    if (is_array_storage(var)) {
        // chain getelemptr for multi-dimensional array access
        IRValue* current_ptr = var;
        const IRType* current_elem_type = get_array_storage_type(current_ptr);

        if (current_elem_type->is_array()) {
            current_ptr = lower_lval_array_address(ast, current_ptr, current_elem_type, allow_array_decay);
        } else if (current_elem_type->is_pointer()) {
            current_ptr = lower_lval_pointer_address(ast, current_ptr, current_elem_type, allow_array_decay);
        }
        return current_ptr;
    }
    // scalar
    if (!ast.indices.empty()) {
        throw std::runtime_error(ast.ident + " is not array");
    }

    return var;
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

// global const array init
void RewindIRBuilder::process_const_array_init(const ConstInitValAST& init_val,
                                               const std::vector<int32_t>& array_dims,
                                               std::vector<int32_t>& target_buffer,
                                               size_t current_dim_idx)
{
    std::visit(
        overloaded{
            [&](const ConstInitValAST::ConstExprInit& expr_init) {
                const auto& exp = expect_node<ExpAST>(*expr_init.const_exp, "ExpAST");
                target_buffer.push_back(eval_exp(exp));
            },
            [&](const ConstInitValAST::ConstArrayInit& array_init) {
                if (current_dim_idx >= array_dims.size()) {
                    return;
                }

                const size_t start_index = target_buffer.size();
                size_t total_count = 1;
                for (size_t i = current_dim_idx; i < array_dims.size(); ++i) {
                    total_count *= static_cast<size_t>(array_dims[i]);
                }

                const size_t next_dim_idx =
                    current_dim_idx + 1 < array_dims.size() ? current_dim_idx + 1 : current_dim_idx;

                for (const auto& init_base : array_init.const_inits) {
                    const auto& nested =
                        expect_node<ConstInitValAST>(*init_base, "ConstInitValAST");
                    process_const_array_init(nested, array_dims, target_buffer, next_dim_idx);
                }

                while (target_buffer.size() < start_index + total_count) {
                    target_buffer.push_back(0);
                }
            }},
        init_val.payload);
}

// global array init
void RewindIRBuilder::process_array_init_const(const InitValAST& init_val,
                                               const std::vector<int32_t>& array_dims,
                                               std::vector<int32_t>& target_buffer,
                                               size_t current_dim_idx)
{
    std::visit(
        overloaded{
            [&](const InitValAST::ExprInit& expr_init) {
                const auto& exp = expect_node<ExpAST>(*expr_init.exp, "ExpAST");
                target_buffer.push_back(eval_exp(exp));
            },
            [&](const InitValAST::ArrayInit& array_init) {
                if (current_dim_idx >= array_dims.size()) {
                    return;
                }

                const size_t start_index = target_buffer.size();
                size_t total_count = 1;
                for (size_t i = current_dim_idx; i < array_dims.size(); ++i) {
                    total_count *= static_cast<size_t>(array_dims[i]);
                }

                const size_t next_dim_idx =
                    current_dim_idx + 1 < array_dims.size() ? current_dim_idx + 1 : current_dim_idx;

                for (const auto& init_base : array_init.inits) {
                    const auto& nested = expect_node<InitValAST>(*init_base, "InitValAST");
                    process_array_init_const(nested, array_dims, target_buffer, next_dim_idx);
                }

                while (target_buffer.size() < start_index + total_count) {
                    target_buffer.push_back(0);
                }
            }},
        init_val.payload);
}

// local array init
// cooperate local_array_init
void RewindIRBuilder::process_array_init_runtime(const InitValAST& init_val,
                                                 const std::vector<int32_t>& array_dims,
                                                 std::vector<IRValue*>& target_buffer,
                                                 size_t current_dim_idx)
{
    std::visit(
        overloaded{
            [&](const InitValAST::ExprInit& expr_init) {
                const auto& exp = expect_node<ExpAST>(*expr_init.exp, "ExpAST");
                target_buffer.push_back(lower_exp(exp));
            },
            [&](const InitValAST::ArrayInit& array_init) {
                if (current_dim_idx >= array_dims.size()) {
                    return;
                }

                const size_t start_index = target_buffer.size();
                size_t total_count = 1;
                for (size_t i = current_dim_idx; i < array_dims.size(); ++i) {
                    total_count *= static_cast<size_t>(array_dims[i]);
                }

                const size_t next_dim_idx =
                    current_dim_idx + 1 < array_dims.size() ? current_dim_idx + 1 : current_dim_idx;

                for (const auto& init_base : array_init.inits) {
                    const auto& nested = expect_node<InitValAST>(*init_base, "InitValAST");
                    process_array_init_runtime(nested, array_dims, target_buffer, next_dim_idx);
                }

                while (target_buffer.size() < start_index + total_count) {
                    target_buffer.push_back(get_or_create_constant(0, current_ctx_->module()));
                }
            }},
        init_val.payload);
}

// ? To be optimized
void RewindIRBuilder::local_array_init(IRValue* alloc,
                                       const std::vector<int32_t>& array_dims,
                                       const std::vector<IRValue*>& values)
{
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

int32_t RewindIRBuilder::eval_exp(const ExpAST& ast)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return eval_lor_exp(lor_exp);
}

int32_t RewindIRBuilder::eval_lor_exp(const LOrExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& simple) -> int32_t {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                return eval_land_exp(land_exp);
            },
            [&](const LOrExpAST::Binary& binary) -> int32_t {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                auto lhs = eval_lor_exp(lor_exp);
                auto rhs = eval_land_exp(land_exp);
                return lhs || rhs;
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_land_exp(const LAndExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& simple) -> int32_t {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                return eval_eq_exp(eq_exp);
            },
            [&](const LAndExpAST::Binary& binary) -> int32_t {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                auto lhs = eval_land_exp(land_exp);
                auto rhs = eval_eq_exp(eq_exp);
                return lhs && rhs;
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_eq_exp(const EqExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& simple) -> int32_t {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                return eval_rel_exp(rel_exp);
            },
            [&](const EqExpAST::Binary& binary) -> int32_t {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                auto lhs = eval_eq_exp(eq_exp);
                auto rhs = eval_rel_exp(rel_exp);
                return eval_binary_op(binary.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_rel_exp(const RelExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& s) -> int32_t {
                const auto& add_exp =
                    expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                return eval_add_exp(add_exp);
            },
            [&](const RelExpAST::Binary& b) -> int32_t {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                auto lhs = eval_rel_exp(rel_exp);
                auto rhs = eval_add_exp(add_exp);
                return eval_binary_op(b.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_add_exp(const AddExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& s) -> int32_t {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                return eval_mul_exp(mul_exp);
            },
            [&](const AddExpAST::Binary& b) -> int32_t {
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                auto lhs = eval_add_exp(add_exp);
                auto rhs = eval_mul_exp(mul_exp);
                return eval_binary_op(b.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_mul_exp(const MulExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& s) -> int32_t {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                return eval_unary_exp(unary_exp);
            },
            [&](const MulExpAST::Binary& b) -> int32_t {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");
                auto lhs = eval_mul_exp(mul_exp);
                auto rhs = eval_unary_exp(unary_exp);
                return eval_binary_op(b.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_unary_exp(const UnaryExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& p) -> int32_t {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*p.exp, "PrimaryExpAST");
                return eval_primary_exp(primary);
            },
            [&](const UnaryExpAST::Unary& u) -> int32_t {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*u.exp, "UnaryExpAST");
                auto operand = eval_unary_exp(unary_exp);
                switch (u.op) {
                case UnaryOp::PLUS:
                    return +operand;
                case UnaryOp::MINUS:
                    return -operand;
                case UnaryOp::NOT:
                    return !operand;
                }
                throw std::runtime_error("invalid UnaryOp");
            },
            [&](const UnaryExpAST::FuncCall&) -> int32_t {
                throw std::runtime_error("can't handle function call");
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_primary_exp(const PrimaryExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& n) -> int32_t { return n.value; },
            [&](const PrimaryExpAST::Expression& e) -> int32_t {
                const auto& exp = expect_node<ExpAST>(*e.exp, "ExpAST");
                return eval_exp(exp);
            },
            [&](const PrimaryExpAST::LValue& lvalue) -> int32_t {
                const auto& lval = expect_node<LValAST>(*lvalue.lval, "LValAST");
                auto value = lookup_value(lval.ident);

                if (!value) {
                    throw std::runtime_error("undefined variable: " + lval.ident);
                }

                if (std::holds_alternative<SymbolTable::Var>(*value)) {
                    throw std::runtime_error(lval.ident + " is not constexpr");
                }

                return std::get<SymbolTable::Constant>(*value).value;
            }},
        ast.payload);
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

// FuncContext& RewindIRBuilder::current_ctx()
//{
// return *current_ctx_;
//}
} // namespace rewind_ir
