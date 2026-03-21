#include "koopa_ir_builder.h"

#include <stdexcept>

namespace koopa_ir {
namespace {

    template <typename T>
    const T& ExpectNode(const BaseAST& node, const char* expected_name)
    {
        const auto* p = dynamic_cast<const T*>(&node);
        if (p == nullptr) {
            throw std::runtime_error(std::string("AST type mismatch, expected: ") + expected_name);
        }
        return *p;
    }

} // namespace

IRProgram KoopaIRBuilder::Build(const BaseAST& ast) const
{
    const auto& comp_unit = ExpectNode<CompUnitAST>(ast, "CompUnitAST");
    return LowerCompUnit(comp_unit);
}

IRProgram KoopaIRBuilder::LowerCompUnit(const CompUnitAST& ast) const
{
    IRProgram program;
    const auto& func_def = ExpectNode<FuncDefAST>(*ast.func_def, "FuncDefAST");
    program.functions.push_back(LowerFuncDef(func_def));
    return program;
}

IRFunction KoopaIRBuilder::LowerFuncDef(const FuncDefAST& ast) const
{
    IRFunction function;
    function.name = ast.ident;

    const auto& func_type = ExpectNode<FuncTypeAST>(*ast.func_type, "FuncTypeAST");
    function.ret_type = LowerFuncType(func_type);

    const auto& block = ExpectNode<BlockAST>(*ast.block, "BlockAST");
    function.blocks.push_back(LowerBlock(block));
    return function;
}

std::string KoopaIRBuilder::LowerFuncType(const FuncTypeAST& ast) const
{
    if (ast.type == "int") {
        return "i32";
    }
    throw std::runtime_error("Unsupported SysY function type: " + ast.type);
}

IRBasicBlock KoopaIRBuilder::LowerBlock(const BlockAST& ast) const
{
    IRBasicBlock block;
    block.name = "%entry";

    const auto& stmt = ExpectNode<StmtAST>(*ast.stmt, "StmtAST");
    block.insts.push_back(LowerStmt(stmt));
    return block;
}

IRInstruction KoopaIRBuilder::LowerStmt(const StmtAST& ast) const
{
    IRInstruction inst;
    inst.kind = IRInstruction::Kind::kRet;
    inst.ret_value = ast.number;
    return inst;
}

} // namespace koopa_ir
