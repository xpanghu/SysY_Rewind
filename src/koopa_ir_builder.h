#pragma once

#include <string>

#include "ast.h"
#include "koopa_ir.h"

namespace koopa_ir {

class KoopaIRBuilder {
public:
    IRProgram build(const BaseAST& ast);

private:
    // exp包含所有的指令集以及表达式自身value
    struct LoweredValue {
        IRValue value;
        std::vector<IRInstruction> insts;

        LoweredValue() = default;
    };

    IRValue new_immediate(int value) const;
    IRValue new_virtual_register();
    IRInstruction new_instruction(IRInstruction::Kind kind, IRInstruction::Op op, IRValue lhs, IRValue rhs);

    int next_virtual_register_ = 0;

    IRProgram lower_comp_unit(const CompUnitAST& ast);
    IRFunction lower_func_def(const FuncDefAST& ast);
    std::string lower_func_type(const FuncTypeAST& ast) const;
    IRBasicBlock lower_block(const BlockAST& ast);
    std::vector<IRInstruction> lower_stmt(const StmtAST& ast);
    LoweredValue lower_exp(const ExpAST& ast);
    LoweredValue lower_lor_exp(const LOrExpAST& ast);
    LoweredValue lower_land_exp(const LAndExpAST& ast);
    LoweredValue lower_eq_exp(const EqExpAST& ast);
    LoweredValue lower_rel_exp(const RelExpAST& ast);
    LoweredValue lower_add_exp(const AddExpAST& ast);
    LoweredValue lower_mul_exp(const MulExpAST& ast);
    LoweredValue lower_unary_exp(const UnaryExpAST& ast);
    LoweredValue lower_primary_exp(const PrimaryExpAST& ast);
};

} // namespace koopa_ir
