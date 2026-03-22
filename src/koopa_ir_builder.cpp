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

IRProgram KoopaIRBuilder::build(const BaseAST& ast) const
{
    const auto& comp_unit = expect_node<CompUnitAST>(ast, "CompUnitAST");
    return lower_comp_unit(comp_unit);
}

IRProgram KoopaIRBuilder::lower_comp_unit(const CompUnitAST& ast) const
{
    IRProgram program;
    const auto& func_def = expect_node<FuncDefAST>(*ast.func_def, "FuncDefAST");
    program.functions.push_back(lower_func_def(func_def));
    return program;
}

IRFunction KoopaIRBuilder::lower_func_def(const FuncDefAST& ast) const
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

IRBasicBlock KoopaIRBuilder::lower_block(const BlockAST& ast) const
{
    IRBasicBlock block;
    block.name = "%entry";

    const auto& stmt = expect_node<StmtAST>(*ast.stmt, "StmtAST");
    block.insts.push_back(lower_stmt(stmt));
    return block;
}

IRInstruction KoopaIRBuilder::lower_stmt(const StmtAST& ast) const
{
    IRInstruction inst;
    inst.kind = IRInstruction::Kind::kRet;
    inst.ret_value = ast.number;
    return inst;
}

} // namespace koopa_ir
