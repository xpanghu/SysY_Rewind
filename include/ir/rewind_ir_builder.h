#pragma once

#include "ast.h"
#include "rewind_ir.h"
#include "rewind_ir_type.h"
#include "symbol_table.h"
#include <iosfwd>
#include <string>
#include <unordered_map>

namespace rewind_ir
{

// error code
enum class IRErrorCode {
    SUCCESS = 0,
    INVALID_ARGUMENT = 1,
    GENERATION_ERROR = 2,
};

/*
 * two features:
 * generate IR
 * eval constant
 */
class RewindIRBuilder
{
public:
    IRModule build(const BaseAST& ast);

private:
    // traverse ast and generate IR
    void lower_comp_unit(const CompUnitAST& ast, IRModule& module);
    IRFunction* lower_func_def(const FuncDefAST& ast, IRModule& module);
    const IRType* lower_func_type(const FuncTypeAST& ast, IRModule& module) const;
    void lower_block(const BlockAST& ast, IRModule& module, IRBasicBlock* current_block);
    void lower_const_decl(const ConstDeclAST& ast, IRModule& module);
    void lower_var_decl(const VarDeclAST& ast, IRModule& module, IRBasicBlock* current_block);
    void lower_stmt(const StmtAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_exp(const ExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_lor_exp(const LOrExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_land_exp(const LAndExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_eq_exp(const EqExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_rel_exp(const RelExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_add_exp(const AddExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_mul_exp(const MulExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_unary_exp(const UnaryExpAST& ast, IRModule& module, IRBasicBlock* current_block);
    IRValue* lower_primary_exp(const PrimaryExpAST& ast, IRModule& module, IRBasicBlock* current_block);

    // eval const value, not return IR
    int32_t eval_exp(const ExpAST& ast, IRModule& module);
    int32_t eval_lor_exp(const LOrExpAST& ast, IRModule& module);
    int32_t eval_land_exp(const LAndExpAST& ast, IRModule& module);
    int32_t eval_eq_exp(const EqExpAST& ast, IRModule& module);
    int32_t eval_rel_exp(const RelExpAST& ast, IRModule& module);
    int32_t eval_add_exp(const AddExpAST& ast, IRModule& module);
    int32_t eval_mul_exp(const MulExpAST& ast, IRModule& module);
    int32_t eval_unary_exp(const UnaryExpAST& ast, IRModule& module);
    int32_t eval_primary_exp(const PrimaryExpAST& ast, IRModule& module);

    // if constant exists, will Reuse previous constant
    IRValue* get_or_create_constant(int32_t value, IRModule& module);

    // make virtual register name (%0, %1, ...)
    std::string next_value_name();
    std::string next_alloc_name(const std::string& ident);

    // symbol table
    SymbolTable symbol_table_;

    // cache: int32_t -> IRConstant*
    std::unordered_map<int32_t, IRConstant*> constant_cache_;
    // record the number of value name
    std::unordered_map<std::string, int> alloc_name_counter_;

    // virtual register number
    int value_counter_ = 0;
};

// IR 文本生成器（rewind_ir 是 Koopa IR 的 C++ 形式）
// support three output ways
// 1. emit(std::ostream&) - 流式输出，适用于大程序，避免内存拷贝
// 2. emit(std::string&) - 输出到字符串，适用于需要进一步处理
// 3. emit_to_file(const std::string&) - 直接输出到文件
class IRTextGen
{
public:
    IRTextGen() = default;

    // 流式输出到 ostream（核心接口，零拷贝）
    // 适用于：直接写入文件流、网络流，或大型程序避免内存占用
    IRErrorCode emit(const IRModule& module, std::ostream& out);

    // 输出到字符串（通过 ostringstream 实现）
    // 适用于：需要将 IR 文本传递给其他 API（如 koopa_parse_from_string）
    IRErrorCode emit_to_string(const IRModule& module, std::string& out);

    // 直接输出到文件
    // 适用于：调试、保存中间结果
    IRErrorCode emit_to_file(const IRModule& module, const std::string& file);

private:
    void print_global_value(const IRValue* value, std::ostream& out);
    void print_function(const IRFunction* func, std::ostream& out);
    void print_basic_block(const IRBasicBlock* block, std::ostream& out);
    void print_instruction(const IRValue* inst, std::ostream& out);
    void print_value(const IRValue* value, std::ostream& out);
    void print_type(const IRType* type, std::ostream& out);
    void print_binary_op(IRBinaryOp op, std::ostream& out);
};

} // namespace rewind_ir
