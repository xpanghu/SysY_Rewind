#include "koopa_ir_builder.h"

#include <array>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace koopa_ir {
namespace {

    template <typename T>
    const T& expect_node(const BaseAST& node, const char* expected_name)
    {
        const auto* p = dynamic_cast<const T*>(&node);
        if (p == nullptr) {
            throw std::runtime_error(std::string("AST type mismatch, expected: ") + expected_name);
        }
        return *p;
    }

    void move_insts(std::vector<IRInstruction>& a, std::vector<IRInstruction>& b)
    {
        a.insert(a.begin(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()));
    }

    inline IRInstruction::Op string_to_binary_op(std::string_view op)
    {
        using BinaryOpItem = std::pair<std::string_view, IRInstruction::Op>;
        static constexpr std::array<BinaryOpItem, 13> kBinaryOpMap { {
            { "*", IRInstruction::Op::MUL },
            { "/", IRInstruction::Op::DIV },
            { "%", IRInstruction::Op::MOD },
            { "+", IRInstruction::Op::ADD },
            { "-", IRInstruction::Op::SUB },
            { "&&", IRInstruction::Op::AND },
            { "||", IRInstruction::Op::OR },
            { "==", IRInstruction::Op::EQ },
            { "!=", IRInstruction::Op::NEQ },
            { "<", IRInstruction::Op::LT },
            { ">", IRInstruction::Op::GT },
            { "<=", IRInstruction::Op::LE },
            { ">=", IRInstruction::Op::GE },
        } };

        for (const auto& [token, mapped_op] : kBinaryOpMap) {
            if (token == op) {
                return mapped_op;
            }
        }
        throw std::runtime_error("unsupported op kind: " + std::string(op));
    }

} // namespace

IRValue KoopaIRBuilder::new_immediate(int value) const
{
    IRValue v {};
    v.kind = IRValue::Kind::IMMEDIATE;
    v.imm = value;
    return v;
}

IRValue KoopaIRBuilder::new_virtual_register()
{
    IRValue v {};
    v.kind = IRValue::Kind::VIRTUAL_REGISTER;
    v.virtual_register_name = next_virtual_register_++;
    return v;
}

/*
 * AST为树结构，通过深度优先遍历或者后序遍历来转换为IR
 */
IRProgram KoopaIRBuilder::build(const BaseAST& ast)
{
    next_virtual_register_ = 0;
    const auto& comp_unit = expect_node<CompUnitAST>(ast, "CompUnitAST");
    return lower_comp_unit(comp_unit);
}

IRProgram KoopaIRBuilder::lower_comp_unit(const CompUnitAST& ast)
{
    IRProgram program;
    const auto& func_def = expect_node<FuncDefAST>(*ast.func_def, "FuncDefAST");
    program.functions.push_back(lower_func_def(func_def));
    return program;
}

IRFunction KoopaIRBuilder::lower_func_def(const FuncDefAST& ast)
{
    IRFunction function;
    function.name = ast.ident;

    const auto& func_type = expect_node<FuncTypeAST>(*ast.func_type, "FuncTypeAST");
    function.ret_type = lower_func_type(func_type);

    const auto& block = expect_node<BlockAST>(*ast.block, "BlockAST");
    function.blocks.push_back(lower_block(block));
    return function;
}

std::string KoopaIRBuilder::lower_func_type(const FuncTypeAST& ast) const
{
    if (ast.type == "int") {
        return "i32";
    }
    throw std::runtime_error("unsupported SysY function type: " + ast.type);
}

IRBasicBlock KoopaIRBuilder::lower_block(const BlockAST& ast)
{
    IRBasicBlock block;
    block.name = "entry";

    const auto& stmt = expect_node<StmtAST>(*ast.stmt, "StmtAST");
    auto insts = lower_stmt(stmt);
    block.insts = insts;
    return block;
}

std::vector<IRInstruction> KoopaIRBuilder::lower_stmt(const StmtAST& ast)
{
    switch (ast.kind) {
    case StmtKind::RETURN: {
        if (!ast.return_exp) {
            throw std::runtime_error("return stmt missing expression");
        }

        const auto& exp = expect_node<ExpAST>(*ast.return_exp, "ExpAST");
        auto lowered = lower_exp(exp);

        IRInstruction ret_inst {};
        ret_inst.kind = IRInstruction::Kind::kRet;
        ret_inst.dst = lowered.value;
        lowered.insts.push_back(ret_inst);
        return lowered.insts;
    }
    }
    throw std::runtime_error("unsupported Stmt kind");
}

KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_exp(const ExpAST& ast)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return lower_lor_exp(lor_exp);
}

// koopa IR:
// %0 = or a, b
// %1 = neq %0, 0
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_lor_exp(const LOrExpAST& ast)
{
    if (ast.epk == LOrExpKind::LAND) {
        const auto& land_exp = expect_node<LAndExpAST>(*ast.land_exp, "LAndExpAST");
        return lower_land_exp(land_exp);
    }

    if (ast.epk == LOrExpKind::LORANDLAND) {
        const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
        const auto& land_exp = expect_node<LAndExpAST>(*ast.land_exp, "LAndExpAST");

        auto lor_value = lower_lor_exp(lor_exp);
        auto land_value = lower_land_exp(land_exp);

        LoweredValue result {};
        move_insts(result.insts, lor_value.insts);
        move_insts(result.insts, land_value.insts);

        // %0 = or a, b
        IRInstruction or_inst {
            IRInstruction::Kind::kBinary,
            IRInstruction::Op::OR,
            lor_value.value,
            land_value.value,
            new_virtual_register()
        };
        result.insts.push_back(or_inst);

        // %1 = neq %0, 0
        IRInstruction eq_inst {
            IRInstruction::Kind::kBinary,
            IRInstruction::Op::NEQ,
            or_inst.dst,
            new_immediate(0),
            new_virtual_register()
        };
        result.insts.push_back(eq_inst);
        result.value = eq_inst.dst;

        return result;
    }

    throw std::runtime_error("unsupported LOrExp kind");
}

// koopa IR:
// %0 = eq a, 0
// %1 = eq b, 0
// %2 = and %0, %1
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_land_exp(const LAndExpAST& ast)
{
    if (ast.epk == LAndExpKind::EQ) {
        const auto& eq_exp = expect_node<EqExpAST>(*ast.eq_exp, "EqExpAST");
        return lower_eq_exp(eq_exp);
    }

    if (ast.epk == LAndExpKind::LANDANDEQ) {
        const auto& land_exp = expect_node<LAndExpAST>(*ast.land_exp, "LAndExpAST");
        const auto& eq_exp = expect_node<EqExpAST>(*ast.eq_exp, "EqExpAST");

        auto land_value = lower_land_exp(land_exp);
        auto eq_value = lower_eq_exp(eq_exp);

        LoweredValue result {};
        move_insts(result.insts, land_value.insts);
        move_insts(result.insts, eq_value.insts);

        IRInstruction lhs_nz_inst {
            IRInstruction::Kind::kBinary,
            IRInstruction::Op::NEQ,
            land_value.value,
            new_immediate(0),
            new_virtual_register()
        };
        result.insts.push_back(lhs_nz_inst);

        IRInstruction rhs_nz_inst {
            IRInstruction::Kind::kBinary,
            IRInstruction::Op::NEQ,
            eq_value.value,
            new_immediate(0),
            new_virtual_register(),
        };
        result.insts.push_back(rhs_nz_inst);

        IRInstruction inst {
            IRInstruction::Kind::kBinary,
            IRInstruction::Op::AND,
            lhs_nz_inst.dst,
            rhs_nz_inst.dst,
            new_virtual_register()
        };
        result.insts.push_back(inst);
        result.value = inst.dst;

        return result;
    }

    throw std::runtime_error("unsupported LAndExp kind");
}

// 包括 "==" 和 "!=" 指令
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_eq_exp(const EqExpAST& ast)
{
    if (ast.epk == EqExpKind::REL) {
        const auto& rel_exp = expect_node<RelExpAST>(*ast.rel_exp, "RelExpAST");
        return lower_rel_exp(rel_exp);
    }

    if (ast.epk == EqExpKind::EQANDREL) {
        const auto& eq_exp = expect_node<EqExpAST>(*ast.eq_exp, "EqExpAST");
        const auto& rel_exp = expect_node<RelExpAST>(*ast.rel_exp, "RelExpAST");

        auto eq_value = lower_eq_exp(eq_exp);
        auto rel_value = lower_rel_exp(rel_exp);

        LoweredValue result {};
        move_insts(result.insts, eq_value.insts);
        move_insts(result.insts, rel_value.insts);

        IRInstruction inst {
            IRInstruction::Kind::kBinary,
            string_to_binary_op(ast.op),
            eq_value.value,
            rel_value.value,
            new_virtual_register()
        };

        result.insts.push_back(inst);
        result.value = inst.dst;

        return result;
    }

    throw std::runtime_error("unsupported EqExp kind");
}

// < / > / <= / >=
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_rel_exp(const RelExpAST& ast)
{
    if (ast.epk == RelExpKind::ADD) {
        const auto& add_exp = expect_node<AddExpAST>(*ast.add_exp, "AddExpAST");
        return lower_add_exp(add_exp);
    }

    if (ast.epk == RelExpKind::RELANDADD) {
        const auto& rel_exp = expect_node<RelExpAST>(*ast.rel_exp, "RelExpAST");
        const auto& add_exp = expect_node<AddExpAST>(*ast.add_exp, "AddExpAST");

        auto rel_value = lower_rel_exp(rel_exp);
        auto add_value = lower_add_exp(add_exp);

        LoweredValue result {};
        move_insts(result.insts, rel_value.insts);
        move_insts(result.insts, add_value.insts);

        IRInstruction inst {
            IRInstruction::Kind::kBinary,
            string_to_binary_op(ast.op),
            rel_value.value,
            add_value.value,
            new_virtual_register()
        };
        result.insts.push_back(inst);
        result.value = inst.dst;

        return result;
    }

    throw std::runtime_error("unsupported RelExp kind");
}

// add_exp是右结合，EBNF式中处理add_exp和mul_exp的优先级
// + / -
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_add_exp(const AddExpAST& ast)
{
    if (ast.epk == AddExpKind::ADDANDMUL) {
        const auto& add_exp = expect_node<AddExpAST>(*ast.add_exp, "AddExpAST");
        const auto& mul_exp = expect_node<MulExpAST>(*ast.mul_exp, "MulExpAST");

        auto mul_value = lower_mul_exp(mul_exp);
        auto add_value = lower_add_exp(add_exp);

        LoweredValue result {};
        move_insts(result.insts, mul_value.insts);
        move_insts(result.insts, add_value.insts);

        IRInstruction inst {
            IRInstruction::Kind::kBinary,
            string_to_binary_op(ast.op),
            add_value.value,
            mul_value.value,
            new_virtual_register()
        };

        result.insts.push_back(inst);
        result.value = inst.dst;

        return result;

    } else if (ast.epk == AddExpKind::MUL) {
        const auto& mul_exp = expect_node<MulExpAST>(*ast.mul_exp, "MulExpAST");
        return lower_mul_exp(mul_exp);
    }
    throw std::runtime_error("unsupported AddExp kind");
}

// mul_exp是右结合
// * / %
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_mul_exp(const MulExpAST& ast)
{
    if (ast.epk == MulExpKind::MULANDUNARY) {
        const auto& mul_exp = expect_node<MulExpAST>(*ast.mul_exp, "MulExpAST");
        const auto& unary_exp = expect_node<UnaryExpAST>(*ast.unary_exp, "UnaryExpAST");

        auto mul_value = lower_mul_exp(mul_exp);
        auto unary_value = lower_unary_exp(unary_exp);

        LoweredValue result {};
        move_insts(result.insts, mul_value.insts);
        move_insts(result.insts, unary_value.insts);

        IRInstruction inst {
            IRInstruction::Kind::kBinary,
            string_to_binary_op(ast.op),
            mul_value.value,
            unary_value.value,
            new_virtual_register()
        };

        result.insts.push_back(inst);
        result.value = inst.dst;

        return result;
    } else if (ast.epk == MulExpKind::UNARY) {
        const auto& unary_exp = expect_node<UnaryExpAST>(*ast.unary_exp, "UnaryExpAST");
        return lower_unary_exp(unary_exp);
    }

    throw std::runtime_error("unsupported MulExp kind");
}

// 一元表达式, koopa_ir中不存在一元表达式，使用二元表达式来替代
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_unary_exp(const UnaryExpAST& ast)
{
    if (ast.epk == UnaryExpKind::PRIMARY) {
        const auto& primary_exp = expect_node<PrimaryExpAST>(*ast.exp, "PrimaryExpAST");
        return lower_primary_exp(primary_exp);
    } else if (ast.epk == UnaryExpKind::OP) {
        const auto& unary_exp = expect_node<UnaryExpAST>(*ast.exp, "UnaryExpAST");
        auto unary_value = lower_unary_exp(unary_exp);

        LoweredValue result {};
        move_insts(result.insts, unary_value.insts);

        // '+'指令无意义
        if (ast.op == "+") {
            // Unary plus is a semantic no-op: just forward the source value.
            result.value = unary_value.value;
            return result;
        }

        // '-'指令右操作数为rhs.value
        if (ast.op == "-") {
            IRInstruction inst {
                IRInstruction::Kind::kBinary,
                IRInstruction::Op::SUB,
                new_immediate(0),
                unary_value.value,
                new_virtual_register(),
            };
            result.insts.push_back(inst);
            result.value = inst.dst;
            return result;
        }

        // '!'指令左操作数为rhs.value
        if (ast.op == "!") {
            IRInstruction inst {
                IRInstruction::Kind::kBinary,
                IRInstruction::Op::EQ,
                new_immediate(0),
                unary_value.value,
                new_virtual_register(),
            };
            result.insts.push_back(inst);
            result.value = inst.dst;
            return result;
        }

        throw std::runtime_error("unsupported unary operator: " + ast.op);
    }
    throw std::runtime_error("unsupported UnaryExp kind");
}

KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_primary_exp(const PrimaryExpAST& ast)
{
    LoweredValue lowered {};

    if (ast.epk == PrimaryExpKind::NUM) {
        lowered.value = new_immediate(ast.number);
        return lowered;
    }

    if (ast.epk == PrimaryExpKind::EXP) {
        const auto& exp = expect_node<ExpAST>(*ast.exp, "ExpAST");
        return lower_exp(exp);
    }

    throw std::runtime_error("unsupported PrimaryExp kind");
}

} // namespace koopa_ir
