#include "rewind_ir.h"
#include "symbol_table.h"
#include "unordered_map"

namespace rewind_ir {

class RewindIRBuilder {
public:
    IRModule build(const BaseAST& ast);

private:
    // AST 遍历和 IR 生成
    void lower_comp_unit(const CompUnitAST& ast, IRModule& module);
    IRFunction* lower_func_def(const FuncDefAST& ast, IRModule& module);
    IRValueType lower_func_type(const FuncTypeAST& ast, IRModule& module) const;
    void lower_block(const BlockAST& ast, IRModule& module, IRBasicBlock* block);
    void lower_stmt(const StmtAST& ast, IRModule& module, IRBasicBlock* block);
    IRValue* lower_exp(const ExpAST& ast, IRModule& module);
    IRValue* lower_lor_exp(const LOrExpAST& ast, IRModule& module);
    IRValue* lower_land_exp(const LAndExpAST& ast, IRModule& module);
    IRValue* lower_eq_exp(const EqExpAST& ast, IRModule& module);
    IRValue* lower_rel_exp(const RelExpAST& ast, IRModule& module);
    IRValue* lower_add_exp(const AddExpAST& ast, IRModule& module);
    IRValue* lower_mul_exp(const MulExpAST& ast, IRModule& module);
    IRValue* lower_unary_exp(const UnaryExpAST& ast, IRModule& module);
    IRValue* lower_primary_exp(const PrimaryExpAST& ast, IRModule& module);

    // 常量表达式求值（不生成 IR，只返回 int32_t 值）
    int32_t eval_exp(const ExpAST& ast, IRModule& module);
    int32_t eval_lor_exp(const LOrExpAST& ast, IRModule& module);
    int32_t eval_land_exp(const LAndExpAST& ast, IRModule& module);
    int32_t eval_eq_exp(const EqExpAST& ast, IRModule& module);
    int32_t eval_rel_exp(const RelExpAST& ast, IRModule& module);
    int32_t eval_add_exp(const AddExpAST& ast, IRModule& module);
    int32_t eval_mul_exp(const MulExpAST& ast, IRModule& module);
    int32_t eval_unary_exp(const UnaryExpAST& ast, IRModule& module);
    int32_t eval_primary_exp(const PrimaryExpAST& ast, IRModule& module);

    // 常量获取或创建（带缓存）
    IRValue* get_or_create_constant(int32_t value, IRModule& module);

    // 符号表
    SymbolTable symbol_table_;

    // 常量缓存：值 -> IRConstant*
    std::unordered_map<int32_t, IRConstant*> constant_cache_;
};
}