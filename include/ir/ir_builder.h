#pragma once

#include "ast.h"
#include "func_context.h"
#include "rewind_ir.h"
#include "ir_type.h"
#include "symbol_table.h"
#include <iosfwd>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <variant>

namespace rewind_ir
{

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
    const IRType* lower_func_type(const FuncTypeAST& ast) const;
    void lower_block(const BlockAST& ast, FuncContext& ctx);
    void lower_const_decl(const ConstDeclAST& ast, FuncContext& ctx);
    void lower_var_decl(const VarDeclAST& ast, FuncContext& ctx);
    void lower_stmt(const StmtAST& ast, FuncContext& ctx);
    IRValue* lower_exp(const ExpAST& ast, FuncContext& ctx);
    IRValue* lower_lor_exp(const LOrExpAST& ast, FuncContext& ctx);
    IRValue* lower_land_exp(const LAndExpAST& ast, FuncContext& ctx);
    IRValue* lower_eq_exp(const EqExpAST& ast, FuncContext& ctx);
    IRValue* lower_rel_exp(const RelExpAST& ast, FuncContext& ctx);
    IRValue* lower_add_exp(const AddExpAST& ast, FuncContext& ctx);
    IRValue* lower_mul_exp(const MulExpAST& ast, FuncContext& ctx);
    IRValue* lower_unary_exp(const UnaryExpAST& ast, FuncContext& ctx);
    IRValue* lower_primary_exp(const PrimaryExpAST& ast, FuncContext& ctx);

    // eval const value, not return IR
    int32_t eval_exp(const ExpAST& ast, const FuncContext& ctx);
    int32_t eval_lor_exp(const LOrExpAST& ast, const FuncContext& ctx);
    int32_t eval_land_exp(const LAndExpAST& ast, const FuncContext& ctx);
    int32_t eval_eq_exp(const EqExpAST& ast, const FuncContext& ctx);
    int32_t eval_rel_exp(const RelExpAST& ast, const FuncContext& ctx);
    int32_t eval_add_exp(const AddExpAST& ast, const FuncContext& ctx);
    int32_t eval_mul_exp(const MulExpAST& ast, const FuncContext& ctx);
    int32_t eval_unary_exp(const UnaryExpAST& ast, const FuncContext& ctx);
    int32_t eval_primary_exp(const PrimaryExpAST& ast, const FuncContext& ctx);

    // if constant exists, will Reuse previous constant
    IRValue* get_or_create_constant(int32_t value, IRModule& module);

    // first find in function symbol table
    // then find in module symbol table
    std::optional<std::variant<int32_t, IRValue*>>
    lookup_symbol(const FuncContext& ctx, const std::string& name) const;

    // cache: int32_t -> IRConstant*
    std::unordered_map<int32_t, IRConstant*> constant_cache_;

    // module-scope symbols: global variables, global consts, and function names
    SymbolTable module_symbols_;
};

} // namespace rewind_ir
