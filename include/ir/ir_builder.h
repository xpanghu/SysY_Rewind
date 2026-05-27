#pragma once

#include "func_context.h"
#include "rewind_ir.h"
#include "ir_type.h"
#include "symbol_table.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class BaseAST;
class CompUnitAST;
class FuncDefAST;
class FuncFParamAST;
class BlockAST;
class StmtAST;
class ExpAST;
class LOrExpAST;
class LAndExpAST;
class EqExpAST;
class RelExpAST;
class AddExpAST;
class MulExpAST;
class UnaryExpAST;
class PrimaryExpAST;
class DeclAST;
class ConstDeclAST;
class ConstDefAST;
class ConstInitValAST;
class InitValAST;
class VarDeclAST;
class LValAST;

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
    IRValue* lower_lval_storage_address(const LValAST& ast,
                                        bool allow_array_decay,
                                        bool require_mutable);
    IRValue* lower_call_arg(const ExpAST& ast, const IRType* expected_ty);
    IRValue* lower_lval_array_address(const LValAST& ast, IRValue*, const IRType*, bool allow_array_decay = false);
    IRValue* lower_lval_pointer_address(const LValAST& ast, IRValue*, const IRType*, bool allow_array_decay = false);

    // Array initializer helpers: flatten AST initializers, build aggregates, or emit local stores.
    IRValue* build_array_aggregate_initializer(const IRArrayType* array_type,
                                               const std::vector<int32_t>& flat_values,
                                               size_t& cursor,
                                               IRModule& module);
    void flatten_global_array_initializer(const InitValAST& init_val,
                                          const std::vector<int32_t>& array_dims,
                                          std::vector<int32_t>& target_buffer);
    void flatten_local_runtime_array_initializer(const InitValAST& init_val,
                                                 const std::vector<int32_t>& array_dims,
                                                 std::vector<IRValue*>& target_buffer);
    void emit_local_array_initializer_stores(IRValue* alloc,
                                             const std::vector<int32_t>& array_dims,
                                             const std::vector<IRValue*>& values);
    void flatten_const_array_initializer(const ConstInitValAST& init_val,
                                         const std::vector<int32_t>& array_dims,
                                         std::vector<int32_t>& target_buffer);
    int32_t eval_exp(const ExpAST& ast);

    // if constant exists, will Reuse previous constant
    IRValue* get_or_create_constant(int32_t value, IRModule& module);

    std::optional<SymbolTable::LookupResult>
    lookup_value(const std::string& name) const;

    IRFunction* lookup_function(const std::string& name) const;

    // Non-owning intern cache. IRModule owns each IRConstant created through make_value.
    std::unordered_map<int32_t, IRConstant*> constant_cache_;

    // module-scope symbols: global variables, global consts, function
    SymbolTable module_symbols_;

    // current function context, used for local variable lookup during lowering
    FuncContext* current_ctx_ = nullptr;

    void set_current_ctx(FuncContext* ctx);
};

} // namespace rewind_ir
