#include "ir_builder.h"
#include "ast.h"
#include "rewind_ir.h"
#include "ir_type.h"
#include "symbol_table.h"
#include <cstdint>

#include <stdexcept>
#include <variant>

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

// this function will check if block nullptr
inline IRBasicBlock& require_current_block(FuncContext& ctx)
{
    auto* block = ctx.current_block();
    if (block == nullptr) {
        throw std::runtime_error("current block is not set");
    }
    return *block;
}

inline IRBasicBlock& create_function_block(FuncContext& ctx,
                                           const std::string& hint)
{
    auto& module = ctx.module();
    auto* block = module.make_basic_block(ctx.next_block_name(hint));
    module.append_basic_block(ctx.current_function(), *block);
    return *block;
}

} // namespace

// 定义了 overloaded 结构体，配套 std::variant 使用
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
    const auto& func_def = expect_node<FuncDefAST>(*ast.func_def, "FuncDefAST");
    lower_func_def(func_def, module);
}

IRFunction* RewindIRBuilder::lower_func_def(const FuncDefAST& ast,
                                            IRModule& module)
{
    // define function
    const auto& func_type =
        expect_node<FuncTypeAST>(*ast.func_type, "FuncTypeAST");
    auto type = lower_func_type(func_type);
    auto func = module.make_function(type, ast.ident);

    // set function context
    FuncContext ctx(module, *func);

    // Initialize entry block
    // magic string "%entry"
    auto& basic_block = create_function_block(ctx, "entry");
    ctx.set_current_block(basic_block);

    const auto& block = expect_node<BlockAST>(*ast.block, "BlockAST");
    lower_block(block, ctx);

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
        // 处理常量和变量
        if (auto* decl = dynamic_cast<DeclAST*>(item.get())) {
            if (auto* const_decl = dynamic_cast<ConstDeclAST*>(decl->const_or_var.get())) {
                lower_const_decl(*const_decl, ctx);
            } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(decl->const_or_var.get())) {
                lower_var_decl(*var_decl, ctx);
            } else {
                throw std::runtime_error("no such decl");
            }
        }
    }
}

void RewindIRBuilder::lower_const_decl(const ConstDeclAST& ast, FuncContext& ctx)
{
    for (const auto& def_base : ast.const_defs) {
        const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
        const auto& init = expect_node<ConstInitValAST>(*def.const_init_val,
                                                        "ConstInitValAST");
        const auto& exp = expect_node<ExpAST>(*init.const_exp, "ExpAST");

        // 求值常量表达式
        int32_t value = eval_exp(exp, ctx);

        // 定义常量
        ctx.symbols().define_const(def.ident, value);
    }
}

void RewindIRBuilder::lower_var_decl(const VarDeclAST& ast, FuncContext& ctx)
{
    auto& module = ctx.module();
    auto* i32_ptr_type = get_pointer_type(get_i32_type());

    for (const auto& def_base : ast.var_defs) {
        const auto& def = expect_node<VarDefAST>(*def_base, "VarDefAST");
        std::visit(
            overloaded{
                // int x;
                [&](const VarDefAST::DefEmpty& def) {
                    auto& current_block = require_current_block(ctx);
                    auto alloc = module.make_value<IRAllocInst>(i32_ptr_type, ctx.next_alloc_name(def.ident));
                    module.append_value(current_block, *alloc);
                    ctx.symbols().define_var(def.ident, alloc);
                },
                // int x = 10;
                [&](const VarDefAST::DefValue& def) {
                    // @ident = alloc i32
                    auto& current_block = require_current_block(ctx);
                    auto alloc = module.make_value<IRAllocInst>(i32_ptr_type, ctx.next_alloc_name(def.ident));
                    module.append_value(current_block, *alloc);
                    ctx.symbols().define_var(def.ident, alloc);

                    // lower exp
                    // store exp_value, @ident
                    const auto& init_val = expect_node<InitValAST>(*def.init_val, "InitValAST");
                    const auto& exp = expect_node<ExpAST>(*init_val.exp, "ExpAST");
                    auto exp_value = lower_exp(exp, ctx);

                    auto store = module.make_value<IRStoreInst>(exp_value, alloc, get_unit_type());
                    auto& store_block = require_current_block(ctx);
                    module.append_value(store_block, *store);
                }

            },
            def.payload);
    }
}

void RewindIRBuilder::lower_stmt(const StmtAST& ast, FuncContext& ctx)
{
    auto& module = ctx.module();
    std::visit(
        overloaded{
            [&](const StmtAST::Return& ret_stmt) {
                IRValue* ret_value = nullptr;

                // check return exp is exist
                // exp can be empty
                if (ret_stmt.exp) {
                    const auto& exp = expect_node<ExpAST>(*ret_stmt.exp, "ExpAST");
                    ret_value = lower_exp(exp, ctx);
                }

                // def return inst
                auto ret_inst = module.make_value<IRReturnInst>(ret_value);
                auto& current_block = require_current_block(ctx);
                module.append_value(current_block, *ret_inst);

                ctx.clear_current_block();
            },
            [&](const StmtAST::Assign& assign_stmt) {
                // assign
                // store exp_value, alloc
                const auto& exp = expect_node<ExpAST>(*assign_stmt.exp, "ExpAST");
                auto exp_inst = lower_exp(exp, ctx);

                auto value = lookup_symbol(ctx, assign_stmt.LVal);
                // value not exist or value is const throw error
                if (!value) {
                    throw std::runtime_error(assign_stmt.LVal + "is not exist");
                } else if (std::holds_alternative<int32_t>(*value)) {
                    throw std::runtime_error(assign_stmt.LVal + "is not variable");
                }

                auto alloc = std::get<IRValue*>(*value);
                auto store_inst = module.make_value<IRStoreInst>(exp_inst, alloc, get_unit_type());
                auto& current_block = require_current_block(ctx);
                module.append_value(current_block, *store_inst);
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
                auto* cond_value = lower_exp(exp, ctx);
                auto& current_block = require_current_block(ctx);

                // def if_basic_block, else_basic_block  merge_basic_block
                auto& then_bb = create_function_block(ctx, "then");
                IRBasicBlock* else_bb = nullptr;
                if (else_stmt != nullptr) {
                    else_bb = &create_function_block(ctx, "else");
                }
                IRBasicBlock* merge_bb = nullptr;
                if (else_stmt == nullptr) {
                    merge_bb = &create_function_block(ctx, "end");
                }

                // current_block add branch inst
                auto* branch = module.make_value<IRBranchInst>(
                    cond_value,
                    &then_bb,
                    else_stmt != nullptr ? else_bb : merge_bb,
                    get_unit_type());
                module.append_value(current_block, *branch);

                // switch if_basic_block
                ctx.set_current_block(then_bb);
                lower_stmt(if_stmt, ctx);
                IRBasicBlock* then_fallthrough = ctx.current_block();
                // through then_fallthrough decide if add the jump inst
                // consider example : if ( exp ) return exp;  don't need jump inst
                if (then_fallthrough != nullptr) {
                    if (merge_bb == nullptr) {
                        merge_bb = &create_function_block(ctx, "end");
                    }
                    auto* jump = module.make_value<IRJumpInst>(merge_bb,
                                                               get_unit_type());
                    module.append_value(*then_fallthrough, *jump);
                }

                // switch else_basic_block
                IRBasicBlock* else_fallthrough = nullptr;
                if (else_stmt != nullptr) {
                    ctx.set_current_block(*else_bb);
                    lower_stmt(*else_stmt, ctx);
                    else_fallthrough = ctx.current_block();
                    if (else_fallthrough != nullptr) {
                        if (merge_bb == nullptr) {
                            merge_bb = &create_function_block(ctx, "end");
                        }
                        auto* jump = module.make_value<IRJumpInst>(merge_bb,
                                                                   get_unit_type());
                        module.append_value(*else_fallthrough, *jump);
                    }
                }

                // merge exists iff at least one path continues, or if without else
                if (merge_bb != nullptr) {
                    ctx.set_current_block(*merge_bb);
                } else {
                    ctx.clear_current_block();
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

                // eval lhs
                auto* lhs = lower_lor_exp(lor_exp, ctx);
                auto& current_block = require_current_block(ctx);

                auto* result_slot = module.make_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    ctx.next_alloc_name("lor_tmp"));
                module.append_value(current_block, *result_slot);

                auto& short_true = create_function_block(ctx, "short_true");
                auto& rhs_bb = create_function_block(ctx, "rhs_basic_block");
                auto& merge = create_function_block(ctx, "merge");

                auto* zero = get_or_create_constant(0, module);
                auto* lhs_bool = module.make_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    ctx.next_value_name());
                module.append_value(current_block, *lhs_bool);
                auto* branch = module.make_value<IRBranchInst>(
                    lhs_bool,
                    &short_true,
                    &rhs_bb,
                    get_unit_type());
                module.append_value(current_block, *branch);

                // short_true basic block
                ctx.set_current_block(short_true);
                auto& short_true_block = require_current_block(ctx);
                auto* one = get_or_create_constant(1, module);
                auto* store_true =
                    module.make_value<IRStoreInst>(one, result_slot, get_unit_type());
                module.append_value(short_true_block, *store_true);
                auto* jump_true =
                    module.make_value<IRJumpInst>(&merge, get_unit_type());
                module.append_value(short_true_block, *jump_true);

                // rhs_bb basic block
                ctx.set_current_block(rhs_bb);
                // eval rhs
                auto* rhs = lower_land_exp(land_exp, ctx);
                auto& rhs_block = require_current_block(ctx);
                auto* rhs_bool = module.make_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    ctx.next_value_name());
                module.append_value(rhs_block, *rhs_bool);

                // store rhs_bool, tmp
                // jump merge
                auto* store_rhs =
                    module.make_value<IRStoreInst>(rhs_bool, result_slot, get_unit_type());
                module.append_value(rhs_block, *store_rhs);
                auto* jump_rhs =
                    module.make_value<IRJumpInst>(&merge, get_unit_type());
                module.append_value(rhs_block, *jump_rhs);

                // merge basic block
                ctx.set_current_block(merge);
                auto& merge_block = require_current_block(ctx);
                auto* result =
                    module.make_value<IRLoadInst>(result_slot, get_i32_type(),
                                                  ctx.next_value_name());
                module.append_value(merge_block, *result);
                return result;
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
                auto& current_block = require_current_block(ctx);

                auto* result_slot = module.make_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    ctx.next_alloc_name("land_tmp"));
                module.append_value(current_block, *result_slot);

                auto& short_false = create_function_block(ctx, "short_false");
                auto& rhs_bb = create_function_block(ctx, "rhs_basic_block");
                auto& merge = create_function_block(ctx, "merge");

                auto* zero = get_or_create_constant(0, module);
                auto* lhs_bool = module.make_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    ctx.next_value_name());
                module.append_value(current_block, *lhs_bool);
                auto* branch = module.make_value<IRBranchInst>(
                    lhs_bool,
                    &rhs_bb,
                    &short_false,
                    get_unit_type());
                module.append_value(current_block, *branch);

                // short_false basic block
                ctx.set_current_block(short_false);
                auto& short_false_block = require_current_block(ctx);
                auto* zero_value = get_or_create_constant(0, module);
                auto* store_false =
                    module.make_value<IRStoreInst>(zero_value, result_slot, get_unit_type());
                module.append_value(short_false_block, *store_false);
                auto* jump_false =
                    module.make_value<IRJumpInst>(&merge, get_unit_type());
                module.append_value(short_false_block, *jump_false);

                // rhs_bb basic block
                ctx.set_current_block(rhs_bb);
                // eval rhs
                auto* rhs = lower_eq_exp(eq_exp, ctx);
                auto& rhs_block = require_current_block(ctx);
                auto* rhs_bool = module.make_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    ctx.next_value_name());
                module.append_value(rhs_block, *rhs_bool);

                // store rhs_bool, tmp
                // jump merge
                auto* store_rhs =
                    module.make_value<IRStoreInst>(rhs_bool, result_slot, get_unit_type());
                module.append_value(rhs_block, *store_rhs);
                auto* jump_rhs =
                    module.make_value<IRJumpInst>(&merge, get_unit_type());
                module.append_value(rhs_block, *jump_rhs);

                // merge basic block
                ctx.set_current_block(merge);
                auto& merge_block = require_current_block(ctx);
                auto* result =
                    module.make_value<IRLoadInst>(result_slot, get_i32_type(),
                                                  ctx.next_value_name());
                module.append_value(merge_block, *result);
                return result;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_eq_exp(const EqExpAST& ast, FuncContext& ctx)
{
    auto& module = ctx.module();
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& s) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*s.rel_exp, "RelExpAST");
                return lower_rel_exp(rel_exp, ctx);
            },
            [&](const EqExpAST::Binary& b) -> IRValue* {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");

                auto lhs = lower_eq_exp(eq_exp, ctx);
                auto rhs = lower_rel_exp(rel_exp, ctx);
                auto& current_block = require_current_block(ctx);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op,
                                                             lhs, rhs, get_i32_type(), ctx.next_value_name());
                module.append_value(current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_rel_exp(const RelExpAST& ast,
                                        FuncContext& ctx)
{
    auto& module = ctx.module();
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
                auto& current_block = require_current_block(ctx);

                auto op = ast_op_to_ir_op(binary.op);
                auto value = module.make_value<IRBinaryInst>(op, lhs, rhs, get_i32_type(), ctx.next_value_name());

                module.append_value(current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_add_exp(const AddExpAST& ast,
                                        FuncContext& ctx)
{
    auto& module = ctx.module();
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
                auto& current_block = require_current_block(ctx);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op, lhs, rhs, get_i32_type(), ctx.next_value_name());

                module.append_value(current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_mul_exp(const MulExpAST& ast,
                                        FuncContext& ctx)
{
    auto& module = ctx.module();
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
                auto& current_block = require_current_block(ctx);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op, lhs, rhs, get_i32_type(), ctx.next_value_name());

                module.append_value(current_block, *value);
                return value;
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
                // u.exp 可能是另一个 UnaryExpAST（嵌套一元运算）或 PrimaryExpAST
                const auto& unary_exp = expect_node<UnaryExpAST>(*unary.exp, "UnaryExpAST");
                auto operand = lower_unary_exp(unary_exp, ctx);
                auto& current_block = require_current_block(ctx);

                // 一元运算符转换为二元运算
                auto zero = get_or_create_constant(0, module);
                switch (unary.op) {
                case UnaryOp::PLUS:
                    return operand; // +x = x
                case UnaryOp::MINUS: {
                    auto value = module.make_value<IRBinaryInst>(IRBinaryOp::SUB,
                                                                 zero, operand, get_i32_type(), ctx.next_value_name());
                    module.append_value(current_block, *value);
                    return value;
                }
                case UnaryOp::NOT: {
                    auto value = module.make_value<IRBinaryInst>(IRBinaryOp::EQ,
                                                                 operand, zero, get_i32_type(), ctx.next_value_name());
                    module.append_value(current_block, *value);
                    return value;
                }
                }
                throw std::runtime_error("invalid UnaryOp");
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_primary_exp(const PrimaryExpAST& ast,
                                            FuncContext& ctx)
{
    auto& module = ctx.module();
    auto& current_block = require_current_block(ctx);
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
                const auto& sym = lookup_symbol(ctx, lvalue.ident);

                if (sym) {
                    const auto& value = *sym;
                    if (std::holds_alternative<int32_t>(value)) {
                        return get_or_create_constant(std::get<int32_t>(value), module);
                    } else {
                        IRValue* alloc = std::get<IRValue*>(value);
                        auto load = module.make_value<IRLoadInst>(alloc, get_i32_type(), ctx.next_value_name());
                        module.append_value(current_block, *load);
                        return load;
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
                auto sym = lookup_symbol(ctx, l.ident);
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
RewindIRBuilder::lookup_symbol(const FuncContext& ctx, const std::string& name) const
{
    if (auto local = ctx.symbols().lookup(name)) {
        return local;
    }
    return module_symbols_.lookup(name);
}
} // namespace rewind_ir
