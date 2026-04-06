#include "rewind_ir_builder.h"
#include "ast.h"
#include "rewind_ir.h"
#include "rewind_ir_type.h"
#include "symbol_table.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace rewind_ir
{
namespace
{

// Helper to get i32 type
inline const IRType* get_i32_type()
{
    return IRTypeContext::instance().getInt32();
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
    IRModule module{};
    const auto& comp_unit = expect_node<CompUnitAST>(ast, "CompUnitAST");
    lower_comp_unit(comp_unit, module);
    return module;
}

int32_t RewindIRBuilder::eval_exp(const ExpAST& ast, IRModule& module)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return eval_lor_exp(lor_exp, module);
}

int32_t RewindIRBuilder::eval_lor_exp(const LOrExpAST& ast, IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& simple) -> int32_t {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                return eval_land_exp(land_exp, module);
            },
            [&](const LOrExpAST::Binary& binary) -> int32_t {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                auto lhs = eval_lor_exp(lor_exp, module);
                auto rhs = eval_land_exp(land_exp, module);
                return lhs || rhs;
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_land_exp(const LAndExpAST& ast,
                                       IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& simple) -> int32_t {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                return eval_eq_exp(eq_exp, module);
            },
            [&](const LAndExpAST::Binary& binary) -> int32_t {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                auto lhs = eval_land_exp(land_exp, module);
                auto rhs = eval_eq_exp(eq_exp, module);
                return lhs && rhs;
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_eq_exp(const EqExpAST& ast, IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& simple) -> int32_t {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                return eval_rel_exp(rel_exp, module);
            },
            [&](const EqExpAST::Binary& binary) -> int32_t {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                auto lhs = eval_eq_exp(eq_exp, module);
                auto rhs = eval_rel_exp(rel_exp, module);
                return eval_binary_op(binary.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_rel_exp(const RelExpAST& ast, IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& s) -> int32_t {
                const auto& add_exp =
                    expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                return eval_add_exp(add_exp, module);
            },
            [&](const RelExpAST::Binary& b) -> int32_t {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                auto lhs = eval_rel_exp(rel_exp, module);
                auto rhs = eval_add_exp(add_exp, module);
                return eval_binary_op(b.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_add_exp(const AddExpAST& ast, IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& s) -> int32_t {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                return eval_mul_exp(mul_exp, module);
            },
            [&](const AddExpAST::Binary& b) -> int32_t {
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                auto lhs = eval_add_exp(add_exp, module);
                auto rhs = eval_mul_exp(mul_exp, module);
                return eval_binary_op(b.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_mul_exp(const MulExpAST& ast, IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& s) -> int32_t {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                return eval_unary_exp(unary_exp, module);
            },
            [&](const MulExpAST::Binary& b) -> int32_t {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");
                auto lhs = eval_mul_exp(mul_exp, module);
                auto rhs = eval_unary_exp(unary_exp, module);
                return eval_binary_op(b.op, lhs, rhs);
            }},
        ast.payload);
}

int32_t RewindIRBuilder::eval_unary_exp(const UnaryExpAST& ast, IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& p) -> int32_t {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*p.exp, "PrimaryExpAST");
                return eval_primary_exp(primary, module);
            },
            [&](const UnaryExpAST::Unary& u) -> int32_t {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*u.exp, "UnaryExpAST");
                auto operand = eval_unary_exp(unary_exp, module);
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
                                          IRModule& module)
{
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& n) -> int32_t { return n.value; },
            [&](const PrimaryExpAST::Expression& e) -> int32_t {
                const auto& exp = expect_node<ExpAST>(*e.exp, "ExpAST");
                return eval_exp(exp, module);
            },
            [&](const PrimaryExpAST::LValue& l) -> int32_t {
                auto sym = symbol_table_.lookup(l.ident);
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
    auto type = lower_func_type(func_type, module);
    auto func = module.make_function(type, ast.ident);

    // reset virtual register
    value_counter_ = 0;

    // enter function scope
    symbol_table_.enter_scope();

    // traverse basic_blocks
    const auto& block = expect_node<BlockAST>(*ast.block, "BlockAST");
    auto basic_block = lower_block(block, module);
    if (basic_block != nullptr) {
        module.append_basic_block(*func, *basic_block);
    }

    // exit function scope
    symbol_table_.exit_scope();

    return func;
}

const IRType* RewindIRBuilder::lower_func_type(const FuncTypeAST& ast,
                                               IRModule& module) const
{
    auto& ctx = IRTypeContext::instance();
    if (ast.type == "int") {
        return ctx.getInt32();
    } else if (ast.type == "void") {
        return ctx.getUnit();
    }
    throw std::runtime_error("unsupported FuncTypeAST");
}

IRBasicBlock* RewindIRBuilder::lower_block(const BlockAST& ast, IRModule& module)
{
    // create empty block
    auto* basic_block = module.make_basic_block("%entry");

    for (const auto& item : ast.items) {
        if (auto* stmt = dynamic_cast<StmtAST*>(item.get())) {
            lower_stmt(*stmt, module, basic_block);
        }
        // 处理常量和变量
        if (auto* decl = dynamic_cast<DeclAST*>(item.get())) {
            if (auto* const_decl = dynamic_cast<ConstDeclAST*>(decl->const_or_var.get())) {
                lower_const_decl(*const_decl, module);
            } else if (auto* var_decl = dynamic_cast<VarDeclAST*>(decl->const_or_var.get())) {
                lower_var_decl(*var_decl, module, basic_block);
            } else {
                throw std::runtime_error("no such decl");
            }
        }
    }
    return basic_block;
}

void RewindIRBuilder::lower_const_decl(const ConstDeclAST& ast, IRModule& module)
{
    for (const auto& def_base : ast.const_defs) {
        const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
        const auto& init = expect_node<ConstInitValAST>(*def.const_init_val,
                                                        "ConstInitValAST");
        const auto& exp = expect_node<ExpAST>(*init.const_exp, "ExpAST");

        // 求值常量表达式
        int32_t value = eval_exp(exp, module);

        // 定义常量
        symbol_table_.define_const(def.ident, value);
    }
}

void RewindIRBuilder::lower_var_decl(const VarDeclAST& ast, IRModule& module, IRBasicBlock* current_block)
{
    auto* i32_ptr_type = IRTypeContext::instance().getPointer(IRTypeContext::instance().getInt32());

    for (const auto& def_base : ast.var_defs) {
        const auto& def = expect_node<VarDefAST>(*def_base, "VarDefAST");
        std::visit(
            overloaded{
                [&](const VarDefAST::DefEmpty& def) {
                    auto alloc = module.make_value<IRAllocInst>(i32_ptr_type, "@" + def.ident);
                    module.append_inst(*current_block, *alloc);
                    symbol_table_.define_var(def.ident, alloc);
                },
                [&](const VarDefAST::DefValue& def) {
                    // @ident = alloc i32
                    auto alloc = module.make_value<IRAllocInst>(i32_ptr_type, "@" + def.ident);
                    module.append_inst(*current_block, *alloc);
                    symbol_table_.define_var(def.ident, alloc);

                    // lower exp
                    // store exp_value, @ident
                    const auto& init_val = expect_node<InitValAST>(*def.init_val, "InitValAST");
                    const auto& exp = expect_node<ExpAST>(*init_val.exp, "ExpAST");
                    auto exp_value = lower_exp(exp, module, current_block);

                    auto store = module.make_value<IRStoreInst>(exp_value, alloc);
                    module.append_inst(*current_block, *store);
                }

            },
            def.payload);
    }
}

void RewindIRBuilder::lower_stmt(const StmtAST& ast, IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const StmtAST::Return& ret) {
                const auto& exp = expect_node<ExpAST>(*ret.exp, "ExpAST");
                auto exp_value = lower_exp(exp, module, current_block);
                auto ret_inst = module.make_value<IRReturnInst>(exp_value);

                module.append_inst(*current_block, *ret_inst);
            },
            [&](const StmtAST::Assign& assign) {
                // assign
                // store exp_value, alloc
                const auto& exp = expect_node<ExpAST>(*assign.exp, "ExpAST");
                auto exp_value = lower_exp(exp, module, current_block);

                auto value = symbol_table_.lookup(assign.LVal);
                // value not exist or value is const throw error
                if (!value) {
                    throw std::runtime_error(assign.LVal + "is not exist");
                } else if (std::holds_alternative<int32_t>(*value)) {
                    throw std::runtime_error(assign.LVal + "is not variable");
                }

                auto alloc = std::get<IRValue*>(*value);
                auto store_inst = module.make_value<IRStoreInst>(exp_value, alloc);
                module.append_inst(*current_block, *store_inst);
            },
            [&](const auto& other) {
                std::string type_name = typeid(other).name();
                throw std::runtime_error("Unsupported statement type: " + type_name);
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_exp(const ExpAST& ast, IRModule& module, IRBasicBlock* current_block)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return lower_lor_exp(lor_exp, module, current_block);
}

// a || b == !(a || b)
IRValue* RewindIRBuilder::lower_lor_exp(const LOrExpAST& ast, IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& s) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*s.land_exp, "LAndExpAST");
                return lower_land_exp(land_exp, module, current_block);
            },
            [&](const LOrExpAST::Binary& b) -> IRValue* {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*b.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*b.land_exp, "LAndExpAST");

                auto lhs = lower_lor_exp(lor_exp, module, current_block);
                auto rhs = lower_land_exp(land_exp, module, current_block);

                auto or_value = module.make_value<IRBinaryInst>(
                    IRBinaryOp::OR, lhs, rhs, get_i32_type(), next_value_name());
                module.append_inst(*current_block, *or_value);

                auto zero = get_or_create_constant(0, module);
                auto lor_inst = module.make_value<IRBinaryInst>(IRBinaryOp::NEQ,
                                                                or_value, zero, get_i32_type(), next_value_name());
                module.append_inst(*current_block, *lor_inst);
                return lor_inst;
            }},
        ast.payload);
}

// a && b == (!a) & (!b)
IRValue* RewindIRBuilder::lower_land_exp(const LAndExpAST& ast,
                                         IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& s) -> IRValue* {
                const auto& eq_exp = expect_node<EqExpAST>(*s.eq_exp, "EqExpAST");
                return lower_eq_exp(eq_exp, module, current_block);
            },
            [&](const LAndExpAST::Binary& b) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*b.land_exp, "LAndExpAST");
                const auto& eq_exp = expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");

                auto lhs = lower_land_exp(land_exp, module, current_block);
                auto rhs = lower_eq_exp(eq_exp, module, current_block);
                auto zero = get_or_create_constant(0, module);

                // (!lhs)
                auto nlhs =
                    module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, lhs, zero, get_i32_type(), next_value_name());
                module.append_inst(*current_block, *nlhs);

                // (!rhs)
                auto nrhs =
                    module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, rhs, zero, get_i32_type(), next_value_name());
                module.append_inst(*current_block, *nrhs);

                auto value =
                    module.make_value<IRBinaryInst>(IRBinaryOp::AND, nlhs, nrhs, get_i32_type(), next_value_name());
                module.append_inst(*current_block, *value);

                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_eq_exp(const EqExpAST& ast, IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& s) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*s.rel_exp, "RelExpAST");
                return lower_rel_exp(rel_exp, module, current_block);
            },
            [&](const EqExpAST::Binary& b) -> IRValue* {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");

                auto lhs = lower_eq_exp(eq_exp, module, current_block);
                auto rhs = lower_rel_exp(rel_exp, module, current_block);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op,
                                                             lhs, rhs, get_i32_type(), next_value_name());
                module.append_inst(*current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_rel_exp(const RelExpAST& ast,
                                        IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& s) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                return lower_add_exp(add_exp, module, current_block);
            },
            [&](const RelExpAST::Binary& b) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");

                auto lhs = lower_rel_exp(rel_exp, module, current_block);
                auto rhs = lower_add_exp(add_exp, module, current_block);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op, lhs, rhs, get_i32_type(), next_value_name());

                module.append_inst(*current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_add_exp(const AddExpAST& ast,
                                        IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& s) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                return lower_mul_exp(mul_exp, module, current_block);
            },
            [&](const AddExpAST::Binary& b) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");

                auto lhs = lower_add_exp(add_exp, module, current_block);
                auto rhs = lower_mul_exp(mul_exp, module, current_block);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op, lhs, rhs, get_i32_type(), next_value_name());

                module.append_inst(*current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_mul_exp(const MulExpAST& ast,
                                        IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& s) -> IRValue* {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                return lower_unary_exp(unary_exp, module, current_block);
            },
            [&](const MulExpAST::Binary& b) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");

                auto lhs = lower_mul_exp(mul_exp, module, current_block);
                auto rhs = lower_unary_exp(unary_exp, module, current_block);

                auto op = ast_op_to_ir_op(b.op);
                auto value = module.make_value<IRBinaryInst>(op, lhs, rhs, get_i32_type(), next_value_name());

                module.append_inst(*current_block, *value);
                return value;
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_unary_exp(const UnaryExpAST& ast,
                                          IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& unary) -> IRValue* {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*unary.exp, "PrimaryExpAST");
                return lower_primary_exp(primary, module, current_block);
            },
            [&](const UnaryExpAST::Unary& unary) -> IRValue* {
                // u.exp 可能是另一个 UnaryExpAST（嵌套一元运算）或 PrimaryExpAST
                const auto& unary_exp = expect_node<UnaryExpAST>(*unary.exp, "UnaryExpAST");
                auto operand = lower_unary_exp(unary_exp, module, current_block);

                // 一元运算符转换为二元运算
                auto zero = get_or_create_constant(0, module);
                switch (unary.op) {
                case UnaryOp::PLUS:
                    return operand; // +x = x
                case UnaryOp::MINUS: {
                    auto value = module.make_value<IRBinaryInst>(IRBinaryOp::SUB,
                                                                 zero, operand, get_i32_type(), next_value_name());
                    module.append_inst(*current_block, *value);
                    return value;
                }
                case UnaryOp::NOT: {
                    auto value = module.make_value<IRBinaryInst>(IRBinaryOp::EQ,
                                                                 operand, zero, get_i32_type(), next_value_name());
                    module.append_inst(*current_block, *value);
                    return value;
                }
                }
                throw std::runtime_error("invalid UnaryOp");
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_primary_exp(const PrimaryExpAST& ast,
                                            IRModule& module, IRBasicBlock* current_block)
{
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& number) -> IRValue* {
                return get_or_create_constant(number.value, module);
            },
            [&](const PrimaryExpAST::Expression& expression) -> IRValue* {
                const auto& exp = expect_node<ExpAST>(*expression.exp, "ExpAST");
                return lower_exp(exp, module, current_block);
            },
            [&](const PrimaryExpAST::LValue& lvalue) -> IRValue* {
                const auto& sym = symbol_table_.lookup(lvalue.ident);

                if (sym) {
                    const auto& value = *sym;
                    if (std::holds_alternative<int32_t>(value)) {
                        return get_or_create_constant(std::get<int32_t>(value), module);
                    } else {
                        IRValue* alloc = std::get<IRValue*>(value);
                        auto load = module.make_value<IRLoadInst>(alloc, get_i32_type(), next_value_name());
                        module.append_inst(*current_block, *load);
                        return load;
                    }
                }

                throw std::runtime_error("undefined identifier: " + lvalue.ident);
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

std::string RewindIRBuilder::next_value_name()
{
    return "%" + std::to_string(value_counter_++);
}

/*
 * generate IR text
 */

IRErrorCode IRTextGen::emit(const IRModule& module, std::ostream& out)
{
    if (module.funcs_.empty()) {
        return IRErrorCode::INVALID_ARGUMENT;
    }

    try {
        for (const auto* value : module.global_values_) {
            print_global_value(value, out);
        }

        for (const auto* func : module.funcs_) {
            print_function(func, out);
        }

        return IRErrorCode::SUCCESS;
    } catch (const std::exception& e) {
        out << "; IRTextGen::emit error: " << e.what() << "\n";
        return IRErrorCode::GENERATION_ERROR;
    }
}

IRErrorCode IRTextGen::emit_to_string(const IRModule& module, std::string& out)
{
    std::ostringstream oss;
    auto ret = emit(module, oss);
    if (ret == IRErrorCode::SUCCESS) {
        out = oss.str();
    }
    return ret;
}

IRErrorCode IRTextGen::emit_to_file(const IRModule& module, const std::string& file)
{
    std::ofstream out(file);
    if (!out) {
        return IRErrorCode::GENERATION_ERROR;
    }
    return emit(module, out);
}

// todo
void IRTextGen::print_global_value(const IRValue* value, std::ostream& out)
{
}

void IRTextGen::print_function(const IRFunction* func, std::ostream& out)
{
    // function head：fun @main(): i32 {
    out << "fun @" << func->name_ << "(): ";
    print_type(func->type_, out);
    out << " {\n";

    // print all blocks
    for (const auto* block : func->basic_blocks_) {
        print_basic_block(block, out);
    }

    out << "}\n";
}

void IRTextGen::print_basic_block(const IRBasicBlock* block, std::ostream& out)
{
    // basic block name：%entry:
    out << block->name_ << ":\n";

    // print all insts
    for (const auto* inst : block->insts_) {
        // out << block->insts_.size() << "\n";
        // out << "debug block\n";
        std::ostringstream line;
        print_instruction(inst, line);
        if (!line.str().empty()) {
            out << line.str() << "\n";
        }
    }
}

void IRTextGen::print_instruction(const IRValue* inst, std::ostream& out)
{
    // out << "debug value\n";
    // const not require inst
    if (inst->is_integer()) {
        return;
    }

    if (inst->is_binary()) {
        const auto* binary = inst->as<IRBinaryInst>();
        out << "  " << binary->name_ << " = ";
        print_binary_op(binary->op_, out);
        out << " ";
        print_value(binary->lhs_, out);
        out << ", ";
        print_value(binary->rhs_, out);
        return;
    }

    if (inst->is_ret()) {
        const auto* ret = inst->as<IRReturnInst>();
        out << "  ret ";
        print_value(ret->dst_, out);
        return;
    }

    // local alloc
    // need to print type
    if (inst->is_alloc()) {
        const auto* alloc = inst->as<IRAllocInst>();
        out << "  " << alloc->name_ << " = ";
        // ? not sure to use this type, need to improve
        out << "alloc " << "i32";
        return;
    }

    if (inst->is_load()) {
        const auto* load = inst->as<IRLoadInst>();
        out << "  " << load->name_ << " = ";
        out << "load " << load->src_->name_;
        return;
    }

    if (inst->is_store()) {
        const auto* store = inst->as<IRStoreInst>();
        const auto* value = store->value_;
        out << "  " << "store ";
        if (value->is_binary()) {
            out << value->name_ << ", " << store->dest_->name_;
        } else if (value->is_integer()) {
            const auto* num = value->as<IRConstant>();
            out << num->value_ << ", " << store->dest_->name_;
        }
        return;
    }

    throw std::runtime_error("Unsupported instruction type: " + inst->name_);
}

// print operand
// const or virtual register
void IRTextGen::print_value(const IRValue* value, std::ostream& out)
{
    // const
    if (value->is_integer()) {
        const auto* c = value->as<IRConstant>();
        out << c->value_;
        return;
    }

    // virtual register
    out << value->name_;
}

void IRTextGen::print_type(const IRType* type, std::ostream& out)
{
    if (type == nullptr) {
        out << "void";
        return;
    }

    switch (type->tag) {
    case IRTypeTag::INT32: {
        out << "i32";
        break;
    }
    case IRTypeTag::UNIT: {
        out << "unit";
        break;
    }
    case IRTypeTag::ARRAY: {
        auto* array_type = type->as<IRArrayType>();
        out << "array[" << array_type->length << " x ";
        print_type(array_type->element_type, out);
        out << "]";
        break;
    }
    case IRTypeTag::POINTER: {
        auto* pointer_type = type->as<IRPointerType>();
        out << "pointer<";
        print_type(pointer_type->base_type, out);
        out << ">";
        break;
    }
    case IRTypeTag::FUNCTION: {
        out << "function<";
        out << "(";
        auto* function_type = type->as<IRFunctionType>();
        for (size_t i = 0; i < function_type->params.size(); i++) {
            if (i > 0) out << ", ";
            print_type(function_type->params[i], out);
        }
        out << ") -> ";
        print_type(function_type->return_type, out);
        out << ">";
        break;
    }
    default:
        throw std::runtime_error("Unsupported IR type");
    }
}

void IRTextGen::print_binary_op(IRBinaryOp op, std::ostream& out)
{
    switch (op) {
    case IRBinaryOp::ADD:
        out << "add";
        break;
    case IRBinaryOp::SUB:
        out << "sub";
        break;
    case IRBinaryOp::MUL:
        out << "mul";
        break;
    case IRBinaryOp::DIV:
        out << "div";
        break;
    case IRBinaryOp::MOD:
        out << "mod";
        break;
    case IRBinaryOp::EQ:
        out << "eq";
        break;
    case IRBinaryOp::NEQ:
        out << "ne";
        break;
    case IRBinaryOp::LT:
        out << "lt";
        break;
    case IRBinaryOp::GT:
        out << "gt";
        break;
    case IRBinaryOp::LE:
        out << "le";
        break;
    case IRBinaryOp::GE:
        out << "ge";
        break;
    case IRBinaryOp::AND:
        out << "and";
        break;
    case IRBinaryOp::OR:
        out << "or";
        break;
    case IRBinaryOp::XOR:
        out << "xor";
        break;
    case IRBinaryOp::SHL:
        out << "shl";
        break;
    case IRBinaryOp::SHR:
        out << "shr";
        break;
    case IRBinaryOp::SAR:
        out << "sar";
        break;
    default:
        throw std::runtime_error("Unsupported binary op");
    }
}

} // namespace rewind_ir
