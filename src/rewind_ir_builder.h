#include "rewind_ir.h"
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

    /*
     * 下面函数用于语义分析
    */
    // 作用域管理
    void enter_scope();
    void exit_scope();

    // 常量定义和查询
    void define_const(const std::string& name, int32_t value);
    std::optional<int32_t> lookup_const(const std::string& name) const;

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

    // 符号表：作用域栈 + 常量映射
    std::vector<std::unordered_map<std::string, int32_t>> const_scopes_;
};
}