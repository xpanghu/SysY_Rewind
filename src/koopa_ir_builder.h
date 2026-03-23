#pragma once

#include <string>

#include "../src/ast.h"
#include "koopa_ir.h"

namespace koopa_ir {

class KoopaIRBuilder {
public:
    IRProgram build(const BaseAST& ast);

private:
    // 单个表达式包含的返回值和指令集
    struct LoweredValue {
        IRValue value; // 表达式最终值
        std::vector<IRInstruction> insts; // 计算该值所需的指令
    };

    IRValue new_immediate(int value) const;
    IRValue new_virtual_register();

    int next_virtual_register_ = 0;

    IRProgram lower_comp_unit(const CompUnitAST& ast);
    IRFunction lower_func_def(const FuncDefAST& ast);
    std::string lower_func_type(const FuncTypeAST& ast) const;
    IRBasicBlock lower_block(const BlockAST& ast);
    std::vector<IRInstruction> lower_stmt(const StmtAST& ast);
    LoweredValue lower_exp(const ExpAST& ast);
    LoweredValue lower_unary_exp(const UnaryExpAST& ast);
    LoweredValue lower_primary_exp(const PrimaryExpAST& ast);
};

} // namespace koopa_ir
