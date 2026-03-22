#pragma once

#include <string>

#include "../src/ast.h"
#include "koopa_ir.h"

namespace koopa_ir {

class KoopaIRBuilder {
public:
    IRProgram build(const BaseAST& ast) const;

private:
    IRProgram lower_comp_unit(const CompUnitAST& ast) const;
    IRFunction lower_func_def(const FuncDefAST& ast) const;
    std::string lower_func_type(const FuncTypeAST& ast) const;
    IRBasicBlock lower_block(const BlockAST& ast) const;
    IRInstruction lower_stmt(const StmtAST& ast) const;
};

} // namespace koopa_ir
