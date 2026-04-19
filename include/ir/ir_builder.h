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
    void declare_library_function(IRModule& module);
    IRFunction* declare_function(const FuncDefAST& ast, IRModule& module);
    void lower_global_decl(const DeclAST& ast, IRModule& module);
    IRFunction* lower_func_def(const FuncDefAST& ast, IRModule& module);
    const IRType* lower_func_f_params(const FuncFParamAST& ast);
    void lower_block(const BlockAST& ast);
    void lower_const_decl(const ConstDeclAST& ast);
    void lower_var_decl(const VarDeclAST& ast);
    void lower_stmt(const StmtAST& ast);
    IRValue* lower_exp(const ExpAST& ast);
    IRValue* lower_lor_exp(const LOrExpAST& ast);
    IRValue* lower_land_exp(const LAndExpAST& ast);
    IRValue* lower_eq_exp(const EqExpAST& ast);
    IRValue* lower_rel_exp(const RelExpAST& ast);
    IRValue* lower_add_exp(const AddExpAST& ast);
    IRValue* lower_mul_exp(const MulExpAST& ast);
    IRValue* lower_unary_exp(const UnaryExpAST& ast);
    IRValue* lower_primary_exp(const PrimaryExpAST& ast);
    IRValue* lower_lval_rvalue(const LValAST& ast);
    IRValue* lower_lval_address(const LValAST& ast, bool allow_array_decay = false);
    IRValue* lower_call_arg(const ExpAST& ast, const IRType* expected_ty);

    // eval const value, not return IR
    void processConstArrayInit(const ConstInitValAST& init_val,
                               const std::vector<int32_t>& array_dims,
                               std::vector<int32_t>& target_buffer,
                               size_t current_dim_idx = 0);
    int32_t eval_exp(const ExpAST& ast);
    int32_t eval_lor_exp(const LOrExpAST& ast);
    int32_t eval_land_exp(const LAndExpAST& ast);
    int32_t eval_eq_exp(const EqExpAST& ast);
    int32_t eval_rel_exp(const RelExpAST& ast);
    int32_t eval_add_exp(const AddExpAST& ast);
    int32_t eval_mul_exp(const MulExpAST& ast);
    int32_t eval_unary_exp(const UnaryExpAST& ast);
    int32_t eval_primary_exp(const PrimaryExpAST& ast);

    // if constant exists, will Reuse previous constant
    IRValue* get_or_create_constant(int32_t value, IRModule& module);

    std::optional<SymbolTable::LookupResult>
    lookup_value(const std::string& name) const;

    IRFunction* lookup_function(const std::string& name) const;

    // cache: int32_t -> IRConstant*
    std::unordered_map<int32_t, IRConstant*> constant_cache_;

    // module-scope symbols: global variables, global consts, function
    SymbolTable module_symbols_;

    // current function context, used for local variable lookup during lowering
    FuncContext* current_ctx_ = nullptr;

    void set_current_ctx(FuncContext* ctx);

    // FuncContext& current_ctx();
};

} // namespace rewind_ir
