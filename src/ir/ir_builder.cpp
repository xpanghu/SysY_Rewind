#include "ir_builder.h"
#include "ast.h"
#include "rewind_ir.h"
#include "ir_type.h"
#include "symbol_table.h"
#include <cstddef>
#include <cstdint>

#include <stdexcept>
#include <variant>

int b = 10;
int a = 10 + b;

namespace rewind_ir
{
namespace
{

// get i32 type
inline const IRType* get_i32_type()
{
    return IRTypeContext::instance().getInt32();
}

// get unit type
inline const IRType* get_unit_type()
{
    return IRTypeContext::instance().getUnit();
}

// get pointer type
inline const IRType* get_pointer_type(const IRType* base_type)
{
    return IRTypeContext::instance().getPointer(base_type);
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

IRFunction* declare_external_function(IRModule& module,
                                      SymbolTable& module_symbols,
                                      const std::string& name,
                                      std::vector<const IRType*> param_types,
                                      const IRType* return_type)
{
    auto* func = module.make_function(return_type, name, true);
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
        if (const auto* func_def = dynamic_cast<const FuncDefAST*>(item.get())) {
            declare_function(*func_def, module);
        }
    }

    for (const auto& item : ast.items) {
        if (const auto* global_decl = dynamic_cast<const DeclAST*>(item.get())) {
            lower_gloabl_decl(*global_decl, module);
        }
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

IRFunction* RewindIRBuilder::declare_function(const FuncDefAST& ast,
                                              IRModule& module)
{
    // get function return type
    const auto& func_type = expect_node<FuncTypeAST>(*ast.func_type, "FuncTypeAST");
    auto return_type = lower_func_type(func_type);

    // define function
    auto func = module.make_function(return_type, ast.ident);
    module_symbols_.define_function(ast.ident, func);

    // assign formal args
    // ? Note : not define variable in symbol table
    size_t param_index = 0;
    for (const auto& item : ast.func_f_params) {
        const auto& func_f_param = expect_node<FuncFParamAST>(*item, "FuncFParam");
        auto* arg_ref = module.make_value<IRFuncArgRef>(
            param_index++,
            lower_func_f_params(func_f_param),
            "@" + func_f_param.ident);
        module.append_param(*func, *arg_ref);
    }
    return func;
}

void RewindIRBuilder::lower_gloabl_decl(const DeclAST& ast, IRModule& module)
{
    if (const auto* const_decl = dynamic_cast<const ConstDeclAST*>(ast.const_or_var.get())) {
        for (const auto& def_base : const_decl->const_defs) {
            const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
            const auto& init = expect_node<ConstInitValAST>(*def.const_init_val,
                                                            "ConstInitValAST");
            const auto& exp = expect_node<ExpAST>(*init.const_exp, "ExpAST");

            auto value = eval_exp(exp);
            module_symbols_.define_const(def.ident, value);
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
                    [&](const VarDefAST::DefEmpty& def_empty) {
                        // set zero to the default init of global value
                        auto* zero_init = module.make_value<IRZeroInit>(i32_type);
                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            zero_init,
                            i32_ptr_type,
                            "@" + def_empty.ident);
                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(def_empty.ident, global_alloc);
                    },
                    [&](const VarDefAST::DefValue& def_value) {
                        // eval init value
                        // init value must be constexpr
                        const auto& init_val =
                            expect_node<InitValAST>(*def_value.init_val, "InitValAST");
                        const auto& exp = expect_node<ExpAST>(*init_val.exp, "ExpAST");
                        auto init = get_or_create_constant(eval_exp(exp), module);

                        auto* global_alloc = module.make_value<IRGlobalAllocInst>(
                            init,
                            i32_ptr_type,
                            "@" + def_value.ident);
                        module.append_global_value(*global_alloc);
                        module_symbols_.define_var(def_value.ident, global_alloc);
                    }},
                def.payload);
        }
        return;
    }

    throw std::runtime_error("unsupported global decl type");
}

IRFunction* RewindIRBuilder::lower_func_def(const FuncDefAST& ast,
                                            IRModule& module)
{
    auto* func = lookup_function(ast.ident);
    if (func == nullptr) {
        throw std::runtime_error("undefined function in lowering: " + ast.ident);
    }

    // set function context
    FuncContext ctx(module, *func);

    // Initialize entry block
    // magic string "%entry"
    auto& basic_block = ctx.create_function_block("entry");
    ctx.set_current_block(basic_block);

    auto function_scope = ctx.make_scope();

    // alloc formal args
    for (size_t i = 0; i < ast.func_f_params.size(); ++i) {
        const auto& func_f_param =
            expect_node<FuncFParamAST>(*ast.func_f_params[i], "FuncFParam");

        // alloc variable
        auto& alloc = ctx.create_block_value<IRAllocInst>(
            get_pointer_type(get_i32_type()),
            ctx.next_percent_name());
        ctx.symbols().define_var(func_f_param.ident, &alloc);

        // store arg variable
        static_cast<void>(ctx.create_block_value<IRStoreInst>(
            func->params_[i],
            &alloc,
            get_unit_type()));
    }

    // lower BlockAST
    const auto& block = expect_node<BlockAST>(*ast.block, "BlockAST");
    lower_block(block, ctx);

    /*
     * ensure function have return
     */
    if (ctx.has_current_block()) {
        if (func->type_->is_unit()) {
            static_cast<void>(ctx.terminate_with_return(nullptr));
        } else if (func->type_->is_int32()) {
            static_cast<void>(ctx.terminate_with_return(
                get_or_create_constant(0, module)));
        } else {
            throw std::runtime_error("unsupported function return type");
        }
    }

    return func;
}

const IRType* RewindIRBuilder::lower_func_type(const FuncTypeAST& ast) const
{
    if (ast.type == "int") {
        return get_i32_type();
    } else if (ast.type == "void") {
        return get_unit_type();
    }
    throw std::runtime_error("unsupported FuncTypeAST");
}

// ? todo
const IRType* RewindIRBuilder::lower_func_f_params(const FuncFParamAST& ast)
{
    return get_i32_type();
}

void RewindIRBuilder::lower_block(const BlockAST& ast, FuncContext& ctx)
{
    auto scope = ctx.make_scope();

    for (const auto& item : ast.items) {
        if (!ctx.has_current_block()) {
            break;
        }

        if (auto* stmt = dynamic_cast<StmtAST*>(item.get())) {
            lower_stmt(*stmt, ctx);
        }
        // lower variable and const
        if (auto* decl = dynamic_cast<DeclAST*>(item.get())) {
            if (auto* const_decl = dynamic_cast<ConstDeclAST*>(decl->const_or_var.get())) {
                lower_const_decl(*const_decl, ctx);
            } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(decl->const_or_var.get())) {
                lower_var_decl(*var_decl, ctx);
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
void RewindIRBuilder::lower_const_decl(const ConstDeclAST& ast, FuncContext& ctx)
{
    for (const auto& def_base : ast.const_defs) {
        const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
        const auto& init = expect_node<ConstInitValAST>(*def.const_init_val,
                                                        "ConstInitValAST");
        const auto& exp = expect_node<ExpAST>(*init.const_exp, "ExpAST");

        // eval const exp
        int32_t value = eval_exp(exp, ctx);

        ctx.symbols().define_const(def.ident, value);
    }
}

void RewindIRBuilder::lower_var_decl(const VarDeclAST& ast, FuncContext& ctx)
{
    auto* i32_ptr_type = get_pointer_type(get_i32_type());

    for (const auto& def_base : ast.var_defs) {
        const auto& def = expect_node<VarDefAST>(*def_base, "VarDefAST");
        std::visit(
            overloaded{
                // int x;
                [&](const VarDefAST::DefEmpty& def) {
                    auto& alloc = ctx.create_block_value<IRAllocInst>(
                        i32_ptr_type, ctx.next_at_name(def.ident));
                    ctx.symbols().define_var(def.ident, &alloc);
                },
                // int x = 10;
                [&](const VarDefAST::DefValue& def) {
                    // @ident = alloc i32
                    auto& alloc = ctx.create_block_value<IRAllocInst>(
                        i32_ptr_type, ctx.next_at_name(def.ident));
                    ctx.symbols().define_var(def.ident, &alloc);

                    // lower exp
                    // store exp_value, @ident
                    const auto& init_val = expect_node<InitValAST>(*def.init_val, "InitValAST");

                    const auto& exp = expect_node<ExpAST>(*init_val.exp, "ExpAST");
                    auto exp_value = lower_exp(exp, ctx);

                    static_cast<void>(ctx.create_block_value<IRStoreInst>(
                        exp_value, &alloc, get_unit_type()));
                }

            },
            def.payload);
    }
}

void RewindIRBuilder::lower_stmt(const StmtAST& ast, FuncContext& ctx)
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
                    if (ctx.current_function().type_->is_unit()) {
                        throw std::runtime_error("void function should not return a value");
                    }
                    const auto& exp = expect_node<ExpAST>(*ret_stmt.exp, "ExpAST");
                    ret_value = lower_exp(exp, ctx);
                } else if (ctx.current_function().type_->is_int32()) {
                    ret_value = get_or_create_constant(0, ctx.module());
                }

                static_cast<void>(ctx.terminate_with_return(ret_value));
            },
            [&](const StmtAST::Assign& assign_stmt) {
                // assign
                // store exp_value, alloc
                const auto& exp = expect_node<ExpAST>(*assign_stmt.exp, "ExpAST");
                auto exp_inst = lower_exp(exp, ctx);

                auto value = lookup_value(ctx, assign_stmt.LVal);
                // value not exist or value is const throw error
                if (!value) {
                    throw std::runtime_error(assign_stmt.LVal + "is not exist");
                } else if (std::holds_alternative<int32_t>(*value)) {
                    throw std::runtime_error(assign_stmt.LVal + "is not variable");
                }

                auto alloc = std::get<IRValue*>(*value);
                static_cast<void>(ctx.create_block_value<IRStoreInst>(
                    exp_inst, alloc, get_unit_type()));
            },
            [&](const StmtAST::Block& block_stmt) {
                const auto& block = expect_node<BlockAST>(*block_stmt.block, "BlockAST");
                lower_block(block, ctx);
            },
            [&](const StmtAST::Exp& exp_stmt) {
                // exp is empty, just return
                if (!exp_stmt.exp) {
                    return;
                }
                const auto& exp = expect_node<ExpAST>(*exp_stmt.exp, "ExpAST");
                static_cast<void>(lower_exp(exp, ctx));
            },
            [&](const StmtAST::SelectStmt& select_stmt) {
                // if ( cond ) if_stmt else else_stmt , else_stmt may be empty
                const auto& exp = expect_node<ExpAST>(*select_stmt.exp, "ExpAST");
                const auto& if_stmt = expect_node<StmtAST>(*select_stmt.if_stmt, "StmtAST");
                const StmtAST* else_stmt = select_stmt.else_stmt ? &expect_node<StmtAST>(*select_stmt.else_stmt, "StmtAST") : nullptr;

                // condition value
                auto* cond = lower_exp(exp, ctx);

                // def if_basic_block, else_basic_block  merge_basic_block
                auto& then_bb = ctx.create_function_block("then");
                IRBasicBlock* else_bb = nullptr;
                if (else_stmt != nullptr) {
                    else_bb = &ctx.create_function_block("else");
                }

                IRBasicBlock* merge_bb = nullptr;
                if (else_stmt == nullptr) {
                    merge_bb = &ctx.create_function_block("end");
                }

                // current_block add branch inst
                static_cast<void>(ctx.terminate_with_branch(
                    cond,
                    then_bb,
                    *(else_stmt != nullptr ? else_bb : merge_bb)));

                // switch then_bb
                ctx.set_current_block(then_bb);
                lower_stmt(if_stmt, ctx);
                IRBasicBlock* then_fallthrough = ctx.current_block_or_null();

                // check if then_bb terminated
                // consider example : if ( exp ) return exp;  don't need jump inst
                if (then_fallthrough != nullptr) {
                    if (merge_bb == nullptr) {
                        merge_bb = &ctx.create_function_block("end");
                    }
                    ctx.set_current_block(*then_fallthrough);
                    static_cast<void>(ctx.terminate_with_jump(*merge_bb));
                }

                // switch else_basic_block
                if (else_stmt != nullptr) {
                    ctx.set_current_block(*else_bb);
                    lower_stmt(*else_stmt, ctx);
                    IRBasicBlock* else_fallthrough = ctx.current_block_or_null();

                    // check if else_basic_block terminated
                    if (else_fallthrough != nullptr) {
                        if (merge_bb == nullptr) {
                            merge_bb = &ctx.create_function_block("end");
                        }
                        ctx.set_current_block(*else_fallthrough);
                        static_cast<void>(ctx.terminate_with_jump(*merge_bb));
                    }
                }

                // if_bb and else_bb all terminate, don't need merge_bb
                // this way can prevent emtpy merge_bb
                if (merge_bb != nullptr) {
                    ctx.set_current_block(*merge_bb);
                } else {
                    ctx.clear_current_block();
                }
            },
            [&](const StmtAST::LoopStmt& loop_stmt) {
                const auto& exp = expect_node<ExpAST>(*loop_stmt.exp, "ExpAST");
                const auto& body_stmt = expect_node<StmtAST>(*loop_stmt.body_stmt, "StmtAST");

                auto& while_entry = ctx.create_function_block("while_entry");
                auto& while_body = ctx.create_function_block("while_body");
                auto& end = ctx.create_function_block("end");

                // preheader -> while_entry
                static_cast<void>(ctx.terminate_with_jump(while_entry));

                // while_entry:
                //   evaluate condition
                //   br cond, while_body, end
                ctx.set_current_block(while_entry);
                auto* cond = lower_exp(exp, ctx);
                static_cast<void>(ctx.terminate_with_branch(
                    cond,
                    while_body,
                    end));

                // record break and continue basic block
                ctx.push_loop(end, while_entry);

                // while_body:
                //   lower body
                //   if body still falls through, jump back to while_entry
                ctx.set_current_block(while_body);
                lower_stmt(body_stmt, ctx);

                // check if while_body is terminated
                auto* body_fallthrough = ctx.current_block_or_null();
                if (body_fallthrough != nullptr) {
                    ctx.set_current_block(*body_fallthrough);
                    static_cast<void>(ctx.terminate_with_jump(while_entry));
                }

                // exit loop
                ctx.pop_loop();

                // end:
                //   subsequent statements continue here
                ctx.set_current_block(end);
            },
            [&](const StmtAST::LoopControlStmt& control_stmt) {
                if (!ctx.in_loop()) {
                    if (control_stmt.kind == StmtAST::LoopControlStmt::Kind::Break) {
                        throw std::runtime_error("break used outside while");
                    } else {
                        throw std::runtime_error("continue used outside while");
                    }
                }

                switch (control_stmt.kind) {
                case StmtAST::LoopControlStmt::Kind::Break: {
                    ctx.terminate_with_jump(*ctx.current_loop().break_target);
                    break;
                }
                case StmtAST::LoopControlStmt::Kind::Continue: {
                    ctx.terminate_with_jump(*ctx.current_loop().continue_target);
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
IRValue* RewindIRBuilder::lower_exp(const ExpAST& ast, FuncContext& ctx)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return lower_lor_exp(lor_exp, ctx);
}

// a || b == !(a || b)
// Short-circuit evaluation
/*
 * current basic block
 * lower lhs
 * alloc result
 * lhs_bool : lhs != 0
 * br lhs_bool short_true rhs_bb
 *
 * short_true basic block
 * store 1 tmp
 * jump merge
 *
 * rhs_bb basic block
 * store rhs_value result
 * jump merge
 *
 * merge basic block
 * % = load result
 */
IRValue* RewindIRBuilder::lower_lor_exp(const LOrExpAST& ast, FuncContext& ctx)
{
    auto& module = ctx.module();
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& simple) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                return lower_land_exp(land_exp, ctx);
            },
            [&](const LOrExpAST::Binary& binary) -> IRValue* {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");

                auto& short_true = ctx.create_function_block("short_true");
                auto& rhs_bb = ctx.create_function_block("rhs_basic_block");
                auto& merge = ctx.create_function_block("merge");

                // lower lhs
                auto* lhs = lower_lor_exp(lor_exp, ctx);
                // @result = alloc i32
                auto& result_slot = ctx.create_block_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    ctx.next_at_name("lor_tmp"));

                // lhs_bool : lhs != 0
                auto* zero = get_or_create_constant(0, module);
                auto& lhs_bool = ctx.create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    ctx.next_percent_name());

                // br lhs_bool short_true rhs_bb
                static_cast<void>(ctx.terminate_with_branch(
                    &lhs_bool,
                    short_true,
                    rhs_bb));

                // * short_true basic block
                ctx.set_current_block(short_true);

                // store 1 result
                // jump merge
                auto* one = get_or_create_constant(1, module);
                static_cast<void>(ctx.create_block_value<IRStoreInst>(
                    one, &result_slot, get_unit_type()));
                static_cast<void>(ctx.terminate_with_jump(merge));

                // * rhs_bb basic block
                ctx.set_current_block(rhs_bb);
                // lower rhs
                auto* rhs = lower_land_exp(land_exp, ctx);
                auto& rhs_bool = ctx.create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    ctx.next_percent_name());

                // store rhs_bool, result
                // jump merge
                static_cast<void>(ctx.create_block_value<IRStoreInst>(
                    &rhs_bool, &result_slot, get_unit_type()));
                static_cast<void>(ctx.terminate_with_jump(merge));

                // merge basic block
                ctx.set_current_block(merge);
                return &ctx.create_block_value<IRLoadInst>(
                    &result_slot, get_i32_type(), ctx.next_percent_name());
            }},
        ast.payload);
}

// a && b : evaluate rhs only when lhs is non-zero
IRValue* RewindIRBuilder::lower_land_exp(const LAndExpAST& ast,
                                         FuncContext& ctx)
{
    auto& module = ctx.module();
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& simple) -> IRValue* {
                const auto& eq_exp = expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                return lower_eq_exp(eq_exp, ctx);
            },
            [&](const LAndExpAST::Binary& binary) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                const auto& eq_exp = expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");

                // eval lhs
                auto lhs = lower_land_exp(land_exp, ctx);
                auto& result_slot = ctx.create_block_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    ctx.next_at_name("land_tmp"));

                // initialization basic_block
                auto& short_false = ctx.create_function_block("short_false");
                auto& rhs_bb = ctx.create_function_block("rhs_basic_block");
                auto& merge = ctx.create_function_block("merge");

                auto* zero = get_or_create_constant(0, module);
                auto& lhs_bool = ctx.create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    ctx.next_percent_name());
                static_cast<void>(ctx.terminate_with_branch(
                    &lhs_bool,
                    rhs_bb,
                    short_false));

                // * short_false basic block
                ctx.set_current_block(short_false);
                auto* zero_value = get_or_create_constant(0, module);
                // store 0 result
                // jump merge
                static_cast<void>(ctx.create_block_value<IRStoreInst>(
                    zero_value, &result_slot, get_unit_type()));
                static_cast<void>(ctx.terminate_with_jump(merge));

                // * rhs_bb basic block
                ctx.set_current_block(rhs_bb);
                // eval rhs
                auto* rhs = lower_eq_exp(eq_exp, ctx);
                auto& rhs_bool = ctx.create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    ctx.next_percent_name());

                // store rhs_bool, result
                // jump merge
                static_cast<void>(ctx.create_block_value<IRStoreInst>(
                    &rhs_bool, &result_slot, get_unit_type()));
                static_cast<void>(ctx.terminate_with_jump(merge));

                // merge basic block
                // % = load @result
                ctx.set_current_block(merge);
                return &ctx.create_block_value<IRLoadInst>(
                    &result_slot, get_i32_type(), ctx.next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_eq_exp(const EqExpAST& ast, FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& simple) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                return lower_rel_exp(rel_exp, ctx);
            },
            [&](const EqExpAST::Binary& binary) -> IRValue* {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");

                auto lhs = lower_eq_exp(eq_exp, ctx);
                auto rhs = lower_rel_exp(rel_exp, ctx);
                auto op = ast_op_to_ir_op(binary.op);
                return &ctx.create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), ctx.next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_rel_exp(const RelExpAST& ast,
                                        FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& simple) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*simple.add_exp, "AddExpAST");
                return lower_add_exp(add_exp, ctx);
            },
            [&](const RelExpAST::Binary& binary) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*binary.add_exp, "AddExpAST");

                auto lhs = lower_rel_exp(rel_exp, ctx);
                auto rhs = lower_add_exp(add_exp, ctx);
                auto op = ast_op_to_ir_op(binary.op);
                return &ctx.create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), ctx.next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_add_exp(const AddExpAST& ast,
                                        FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& s) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                return lower_mul_exp(mul_exp, ctx);
            },
            [&](const AddExpAST::Binary& b) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");

                auto lhs = lower_add_exp(add_exp, ctx);
                auto rhs = lower_mul_exp(mul_exp, ctx);
                auto op = ast_op_to_ir_op(b.op);

                return &ctx.create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), ctx.next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_mul_exp(const MulExpAST& ast,
                                        FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& s) -> IRValue* {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                return lower_unary_exp(unary_exp, ctx);
            },
            [&](const MulExpAST::Binary& b) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");

                auto lhs = lower_mul_exp(mul_exp, ctx);
                auto rhs = lower_unary_exp(unary_exp, ctx);
                auto op = ast_op_to_ir_op(b.op);
                return &ctx.create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), ctx.next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_unary_exp(const UnaryExpAST& ast,
                                          FuncContext& ctx)
{
    auto& module = ctx.module();
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& unary) -> IRValue* {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*unary.exp, "PrimaryExpAST");
                return lower_primary_exp(primary, ctx);
            },
            [&](const UnaryExpAST::Unary& unary) -> IRValue* {
                const auto& unary_exp = expect_node<UnaryExpAST>(*unary.exp, "UnaryExpAST");
                auto operand = lower_unary_exp(unary_exp, ctx);

                //
                auto zero = get_or_create_constant(0, module);
                switch (unary.op) {
                case UnaryOp::PLUS:
                    return operand; // +x = x
                case UnaryOp::MINUS: {
                    return &ctx.create_block_value<IRBinaryInst>(
                        IRBinaryOp::SUB, zero, operand, get_i32_type(),
                        ctx.next_percent_name());
                }
                case UnaryOp::NOT: {
                    return &ctx.create_block_value<IRBinaryInst>(
                        IRBinaryOp::EQ, operand, zero, get_i32_type(),
                        ctx.next_percent_name());
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
                    for (const auto& item : func_r_params.exps) {
                        const auto& exp = expect_node<ExpAST>(*item, "ExpAST");
                        args.push_back(lower_exp(exp, ctx));
                    }
                }

                // check if params match
                // from two sides : size and type_
                if (args.size() != callee->params_.size()) {
                    throw std::runtime_error(
                        "function argument count mismatch: " + funcCall.ident);
                }

                for (size_t i = 0; i < args.size(); ++i) {
                    if (args[i]->type_ != callee->params_[i]->type_) {
                        throw std::runtime_error(
                            "function argument type mismatch: " + funcCall.ident);
                    }
                }

                // create call inst
                if (callee->type_->is_unit()) {
                    return &ctx.create_block_value<IRCallInst>(
                        callee,
                        std::move(args),
                        callee->type_);
                }

                return &ctx.create_block_value<IRCallInst>(
                    callee,
                    std::move(args),
                    callee->type_,
                    ctx.next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_primary_exp(const PrimaryExpAST& ast,
                                            FuncContext& ctx)
{
    auto& module = ctx.module();
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& number) -> IRValue* {
                return get_or_create_constant(number.value, module);
            },
            [&](const PrimaryExpAST::Expression& expression) -> IRValue* {
                const auto& exp = expect_node<ExpAST>(*expression.exp, "ExpAST");
                return lower_exp(exp, ctx);
            },
            [&](const PrimaryExpAST::LValue& lvalue) -> IRValue* {
                const auto& sym = lookup_value(ctx, lvalue.ident);

                if (sym) {
                    const auto& value = *sym;
                    if (std::holds_alternative<int32_t>(value)) {
                        // const
                        return get_or_create_constant(std::get<int32_t>(value), module);
                    } else {
                        // local variable or global variable
                        IRValue* alloc = std::get<IRValue*>(value);
                        return &ctx.create_block_value<IRLoadInst>(
                            alloc, get_i32_type(), ctx.next_percent_name());
                    }
                }

                throw std::runtime_error("undefined identifier: " + lvalue.ident);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_exp(const ExpAST& ast, const FuncContext& ctx)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return eval_lor_exp(lor_exp, ctx);
}

int32_t RewindIRBuilder::eval_exp(const ExpAST& ast)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return eval_lor_exp(lor_exp);
}

int32_t RewindIRBuilder::eval_lor_exp(const LOrExpAST& ast, const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& simple) -> int32_t {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                return eval_land_exp(land_exp, ctx);
            },
            [&](const LOrExpAST::Binary& binary) -> int32_t {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                auto lhs = eval_lor_exp(lor_exp, ctx);
                auto rhs = eval_land_exp(land_exp, ctx);
                return lhs || rhs;
            }},
        ast.payload);
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

int32_t RewindIRBuilder::eval_land_exp(const LAndExpAST& ast,
                                       const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& simple) -> int32_t {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                return eval_eq_exp(eq_exp, ctx);
            },
            [&](const LAndExpAST::Binary& binary) -> int32_t {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                auto lhs = eval_land_exp(land_exp, ctx);
                auto rhs = eval_eq_exp(eq_exp, ctx);
                return lhs && rhs;
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

int32_t RewindIRBuilder::eval_eq_exp(const EqExpAST& ast, const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& simple) -> int32_t {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                return eval_rel_exp(rel_exp, ctx);
            },
            [&](const EqExpAST::Binary& binary) -> int32_t {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                auto lhs = eval_eq_exp(eq_exp, ctx);
                auto rhs = eval_rel_exp(rel_exp, ctx);
                return eval_binary_op(binary.op, lhs, rhs);
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

int32_t RewindIRBuilder::eval_rel_exp(const RelExpAST& ast, const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& s) -> int32_t {
                const auto& add_exp =
                    expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                return eval_add_exp(add_exp, ctx);
            },
            [&](const RelExpAST::Binary& b) -> int32_t {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                auto lhs = eval_rel_exp(rel_exp, ctx);
                auto rhs = eval_add_exp(add_exp, ctx);
                return eval_binary_op(b.op, lhs, rhs);
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

int32_t RewindIRBuilder::eval_add_exp(const AddExpAST& ast, const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& s) -> int32_t {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                return eval_mul_exp(mul_exp, ctx);
            },
            [&](const AddExpAST::Binary& b) -> int32_t {
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                auto lhs = eval_add_exp(add_exp, ctx);
                auto rhs = eval_mul_exp(mul_exp, ctx);
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

int32_t RewindIRBuilder::eval_mul_exp(const MulExpAST& ast, const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& s) -> int32_t {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                return eval_unary_exp(unary_exp, ctx);
            },
            [&](const MulExpAST::Binary& b) -> int32_t {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");
                auto lhs = eval_mul_exp(mul_exp, ctx);
                auto rhs = eval_unary_exp(unary_exp, ctx);
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

int32_t RewindIRBuilder::eval_unary_exp(const UnaryExpAST& ast, const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& p) -> int32_t {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*p.exp, "PrimaryExpAST");
                return eval_primary_exp(primary, ctx);
            },
            [&](const UnaryExpAST::Unary& u) -> int32_t {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*u.exp, "UnaryExpAST");
                auto operand = eval_unary_exp(unary_exp, ctx);
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
            [&](const UnaryExpAST::FuncCall& funcCall) -> int32_t {
                throw std::runtime_error("can't handle function call");
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

int32_t RewindIRBuilder::eval_primary_exp(const PrimaryExpAST& ast,
                                          const FuncContext& ctx)
{
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& n) -> int32_t { return n.value; },
            [&](const PrimaryExpAST::Expression& e) -> int32_t {
                const auto& exp = expect_node<ExpAST>(*e.exp, "ExpAST");
                return eval_exp(exp, ctx);
            },
            [&](const PrimaryExpAST::LValue& l) -> int32_t {
                auto sym = lookup_value(ctx, l.ident);
                if (!sym) {
                    throw std::runtime_error("undefined const: " + l.ident);
                } else {
                    const auto& value = *sym;
                    if (std::holds_alternative<int32_t>(value)) {
                        return std::get<int32_t>(value);
                    } else {
                        throw std::runtime_error(std::get<IRValue*>(value)->name_ + " is not const");
                    }
                }
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
            [&](const PrimaryExpAST::LValue& l) -> int32_t {
                auto sym = lookup_value(l.ident);
                if (!sym) {
                    throw std::runtime_error("undefined const: " + l.ident);
                }
                const auto& value = *sym;
                if (std::holds_alternative<int32_t>(value)) {
                    return std::get<int32_t>(value);
                } else {
                    throw std::runtime_error(
                        std::get<IRValue*>(value)->name_ + " is not const");
                }
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

std::optional<std::variant<int32_t, IRValue*>>
RewindIRBuilder::lookup_value(const FuncContext& ctx, const std::string& name) const
{
    if (auto local = ctx.symbols().lookup_value(name)) {
        return local;
    }
    return module_symbols_.lookup_value(name);
}

std::optional<std::variant<int32_t, IRValue*>>
RewindIRBuilder::lookup_value(const std::string& name) const
{
    return module_symbols_.lookup_value(name);
}

IRFunction* RewindIRBuilder::lookup_function(const std::string& name) const
{
    return module_symbols_.lookup_function(name);
}
} // namespace rewind_ir
