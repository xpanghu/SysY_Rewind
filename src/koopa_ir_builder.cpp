#include "koopa_ir_builder.h"

#include <stdexcept>

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
    throw std::runtime_error("Unsupported SysY function type: " + ast.type);
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
    if (ast.kind == StmtKind::RETURN) {
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

    throw std::runtime_error("unsupported Stmt kind");
}

// 自底向上构造 exp
KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_exp(const ExpAST& ast)
{
    const auto& unary_exp = expect_node<UnaryExpAST>(*ast.unary_exp, "UnaryExpAST");
    return lower_unary_exp(unary_exp);
}

KoopaIRBuilder::LoweredValue KoopaIRBuilder::lower_unary_exp(const UnaryExpAST& ast)
{
    if (ast.epk == UnaryExpKind::PRIMARY) {
        const auto& primary_exp = expect_node<PrimaryExpAST>(*ast.exp, "PrimaryExpAST");
        return lower_primary_exp(primary_exp);
    }

    else if (ast.epk == UnaryExpKind::OP) {
        const auto& rhs_unary = expect_node<UnaryExpAST>(*ast.exp, "UnaryExpAST");
        auto rhs = lower_unary_exp(rhs_unary);
        LoweredValue result {};
        result.insts = std::move(rhs.insts);

        if (ast.op == "+") {
            // Unary plus is a semantic no-op: just forward the source value.
            result.value = rhs.value;
            return result;
        }

        IRInstruction inst {};
        inst.kind = IRInstruction::Kind::kUnary;

        if (ast.op == "-") {
            inst.op = IRInstruction::Op::SUB;
            inst.lhs = new_immediate(0);
            inst.rhs = rhs.value;
            inst.dst = new_virtual_register();
            result.insts.push_back(inst);
            result.value = inst.dst;
            return result;
        }

        if (ast.op == "!") {
            inst.op = IRInstruction::Op::EQ;
            inst.lhs = rhs.value;
            inst.rhs = new_immediate(0);
            inst.dst = new_virtual_register();
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
