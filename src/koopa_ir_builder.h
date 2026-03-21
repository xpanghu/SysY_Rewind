#pragma once

#include <string>

#include "../src/ast.h"
#include "koopa_ir.h"

namespace koopa_ir {

class KoopaIRBuilder {
public:
    IRProgram Build(const BaseAST& ast) const;

private:
    IRProgram LowerCompUnit(const CompUnitAST& ast) const;
    IRFunction LowerFuncDef(const FuncDefAST& ast) const;
    std::string LowerFuncType(const FuncTypeAST& ast) const;
    IRBasicBlock LowerBlock(const BlockAST& ast) const;
    IRInstruction LowerStmt(const StmtAST& ast) const;
};

} // namespace koopa_ir
