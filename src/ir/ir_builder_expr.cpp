#include "ir_builder.h"
#include "func_context.h"
#include "ir_builder_internal.h"
#include "semantic_checks.h"
#include "symbol_table.h"
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace rewind_ir
{
using namespace builder_detail;

/*
 * lower_*exp not only return IRValue* also may advance the current insertion point
 */
IRValue* RewindIRBuilder::lower_exp(const ExpAST& ast)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return lower_lor_exp(lor_exp);
}

// Short-circuit evaluation
/*
 * current basic block
 * lower lhs
 * alloc result
 * lhs_bool : lhs != 0
 * br lhs_bool short_true rhs_bb
 *
 * short_true basic block
 * store 1 result
 * jump merge
 *
 * rhs_bb basic block
 * store rhs_value result
 * jump merge
 *
 * merge basic block
 * % = load result
 */
IRValue* RewindIRBuilder::lower_lor_exp(const LOrExpAST& ast)
{
    auto& module = current_ctx_->module();
    return std::visit(
        overloaded{
            [&](const LOrExpAST::Simple& simple) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                return lower_land_exp(land_exp);
            },
            [&](const LOrExpAST::Binary& binary) -> IRValue* {
                const auto& lor_exp =
                    expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");

                auto& short_true = current_ctx_->create_function_block("short_true");
                auto& rhs_bb = current_ctx_->create_function_block("rhs_basic_block");
                auto& merge = current_ctx_->create_function_block("merge");

                // lower lhs
                auto* lhs = lower_lor_exp(lor_exp);

                // @result = alloc i32
                auto& result_slot = current_ctx_->create_block_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    current_ctx_->next_at_name("lor_tmp"));

                // lhs_bool : lhs != 0
                auto* zero = get_or_create_constant(0, module);
                auto& lhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());

                // br lhs_bool short_true rhs_bb
                static_cast<void>(current_ctx_->terminate_with_branch(
                    lhs_bool,
                    short_true,
                    rhs_bb));

                // * short_true basic block
                current_ctx_->set_current_block(short_true);

                // store 1 result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    get_or_create_constant(1, module),
                    &result_slot,
                    get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // * rhs_bb basic block
                current_ctx_->set_current_block(rhs_bb);
                // lower rhs
                auto* rhs = lower_land_exp(land_exp);
                auto& rhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());

                // store rhs_bool, result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    &rhs_bool, &result_slot, get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // merge basic block
                current_ctx_->set_current_block(merge);
                return &current_ctx_->create_block_value<IRLoadInst>(
                    &result_slot,
                    get_i32_type(),
                    current_ctx_->next_percent_name());
            }},
        ast.payload);
}

// a && b : evaluate rhs only when lhs is non-zero
IRValue* RewindIRBuilder::lower_land_exp(const LAndExpAST& ast)
{
    auto& module = current_ctx_->module();
    return std::visit(
        overloaded{
            [&](const LAndExpAST::Simple& simple) -> IRValue* {
                const auto& eq_exp = expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                return lower_eq_exp(eq_exp);
            },
            [&](const LAndExpAST::Binary& binary) -> IRValue* {
                const auto& land_exp =
                    expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                const auto& eq_exp = expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");

                // eval lhs
                auto lhs = lower_land_exp(land_exp);

                auto& result_slot = current_ctx_->create_block_value<IRAllocInst>(
                    get_pointer_type(get_i32_type()),
                    current_ctx_->next_at_name("land_tmp"));

                // initialization basic_block
                auto& short_false = current_ctx_->create_function_block("short_false");
                auto& rhs_bb = current_ctx_->create_function_block("rhs_basic_block");
                auto& merge = current_ctx_->create_function_block("merge");

                auto* zero = get_or_create_constant(0, module);
                auto& lhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    lhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());
                static_cast<void>(current_ctx_->terminate_with_branch(
                    lhs_bool,
                    rhs_bb,
                    short_false));

                // * short_false basic block
                current_ctx_->set_current_block(short_false);
                auto* zero_value = get_or_create_constant(0, module);
                // store 0 result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    zero_value, &result_slot, get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // * rhs_bb basic block
                current_ctx_->set_current_block(rhs_bb);
                // eval rhs
                auto* rhs = lower_eq_exp(eq_exp);
                auto& rhs_bool = current_ctx_->create_block_value<IRBinaryInst>(
                    IRBinaryOp::NEQ,
                    rhs,
                    zero,
                    get_i32_type(),
                    current_ctx_->next_percent_name());

                // store rhs_bool, result
                // jump merge
                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    &rhs_bool, &result_slot, get_unit_type()));
                static_cast<void>(current_ctx_->terminate_with_jump(merge));

                // merge basic block
                // % = load @result
                current_ctx_->set_current_block(merge);
                return &current_ctx_->create_block_value<IRLoadInst>(
                    &result_slot,
                    get_i32_type(),
                    current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_eq_exp(const EqExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const EqExpAST::Simple& simple) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                return lower_rel_exp(rel_exp);
            },
            [&](const EqExpAST::Binary& binary) -> IRValue* {
                const auto& eq_exp =
                    expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");

                auto lhs = lower_eq_exp(eq_exp);
                auto rhs = lower_rel_exp(rel_exp);
                auto op = ast_op_to_ir_op(binary.op);
                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_rel_exp(const RelExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const RelExpAST::Simple& simple) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*simple.add_exp, "AddExpAST");
                return lower_add_exp(add_exp);
            },
            [&](const RelExpAST::Binary& binary) -> IRValue* {
                const auto& rel_exp =
                    expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                const auto& add_exp =
                    expect_node<AddExpAST>(*binary.add_exp, "AddExpAST");

                auto lhs = lower_rel_exp(rel_exp);
                auto rhs = lower_add_exp(add_exp);
                auto op = ast_op_to_ir_op(binary.op);
                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_add_exp(const AddExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const AddExpAST::Simple& simple) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*simple.mul_exp, "MulExpAST");
                return lower_mul_exp(mul_exp);
            },
            [&](const AddExpAST::Binary& binary) -> IRValue* {
                const auto& add_exp =
                    expect_node<AddExpAST>(*binary.add_exp, "AddExpAST");
                const auto& mul_exp =
                    expect_node<MulExpAST>(*binary.mul_exp, "MulExpAST");

                auto lhs = lower_add_exp(add_exp);
                auto rhs = lower_mul_exp(mul_exp);
                auto op = ast_op_to_ir_op(binary.op);

                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_mul_exp(const MulExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const MulExpAST::Simple& simple) -> IRValue* {
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*simple.unary_exp, "UnaryExpAST");
                return lower_unary_exp(unary_exp);
            },
            [&](const MulExpAST::Binary& binary) -> IRValue* {
                const auto& mul_exp =
                    expect_node<MulExpAST>(*binary.mul_exp, "MulExpAST");
                const auto& unary_exp =
                    expect_node<UnaryExpAST>(*binary.unary_exp, "UnaryExpAST");

                auto lhs = lower_mul_exp(mul_exp);
                auto rhs = lower_unary_exp(unary_exp);
                auto op = ast_op_to_ir_op(binary.op);
                return &current_ctx_->create_block_value<IRBinaryInst>(
                    op, lhs, rhs, get_i32_type(), current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_unary_exp(const UnaryExpAST& ast)
{
    auto& module = current_ctx_->module();
    return std::visit(
        overloaded{
            [&](const UnaryExpAST::Primary& unary) -> IRValue* {
                const auto& primary =
                    expect_node<PrimaryExpAST>(*unary.exp, "PrimaryExpAST");
                return lower_primary_exp(primary);
            },
            [&](const UnaryExpAST::Unary& unary) -> IRValue* {
                const auto& unary_exp = expect_node<UnaryExpAST>(*unary.exp, "UnaryExpAST");
                auto operand = lower_unary_exp(unary_exp);

                //
                auto zero = get_or_create_constant(0, module);
                switch (unary.op) {
                case UnaryOp::PLUS:
                    return operand; // +x = x
                case UnaryOp::MINUS: {
                    return &current_ctx_->create_block_value<IRBinaryInst>(
                        IRBinaryOp::SUB, zero, operand, get_i32_type(),
                        current_ctx_->next_percent_name());
                }
                case UnaryOp::NOT: {
                    return &current_ctx_->create_block_value<IRBinaryInst>(
                        IRBinaryOp::EQ, operand, zero, get_i32_type(),
                        current_ctx_->next_percent_name());
                }
                }
                throw std::runtime_error("invalid UnaryOp");
            },
            [&](const UnaryExpAST::FuncCall& funcCall) -> IRValue* {
                // check if function exist
                auto* callee = lookup_function(funcCall.ident);
                semantic::require_function_defined(callee, funcCall.ident);

                // get params
                std::vector<IRValue*> args;
                const size_t actual_count = funcCall.func_r_params != nullptr
                                                ? expect_node<FuncRParamsAST>(
                                                      *funcCall.func_r_params,
                                                      "FuncRParamsAST").exps.size()
                                                : 0;
                semantic::require_call_argument_count(*callee, actual_count, funcCall.ident);

                if (funcCall.func_r_params != nullptr) {
                    const auto& func_r_params =
                        expect_node<FuncRParamsAST>(*funcCall.func_r_params, "FuncRParamsAST");

                    for (size_t i = 0; i < func_r_params.exps.size(); ++i) {
                        const auto& item = func_r_params.exps[i];
                        const auto& exp_ast = expect_node<ExpAST>(*item, "ExpAST");
                        args.push_back(lower_call_arg(exp_ast, callee->type_->params[i]));
                        semantic::require_call_argument_type(
                            args.back()->type_,
                            callee->type_->params[i],
                            funcCall.ident);
                    }
                }

                // create call inst
                if (callee->type_->return_type->is_unit()) {
                    return &current_ctx_->create_block_value<IRCallInst>(
                        callee,
                        std::move(args),
                        callee->type_->return_type);
                }

                return &current_ctx_->create_block_value<IRCallInst>(
                    callee,
                    std::move(args),
                    callee->type_->return_type,
                    current_ctx_->next_percent_name());
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_call_arg(const ExpAST& ast, const IRType* expected_ty)
{
    // scalar
    if (expected_ty->is_int32()) {
        return lower_exp(ast);
    }

    // pointer-like actual argument, mainly array decay
    if (expected_ty->is_pointer()) {
        const auto* lval_ast = try_extract_lvalue(ast);

        // not variable
        if (lval_ast == nullptr) {
            semantic::throw_call_argument_type_mismatch();
        }

        auto lval = lookup_value(lval_ast->ident);
        auto* storage = semantic::require_array_argument_storage(lval).storage;

        IRValue* actual = nullptr;
        IRValue* zero_value = get_or_create_constant(0, current_ctx_->module());

        // two array type:
        // *[i32, ... ]
        // **i32
        if (lval_ast->indices.empty()) {
            const IRType* storage_type = get_array_storage_type(storage);
            if (storage_type->is_array()) {
                const IRType* elem_type = storage_type->as<IRArrayType>()->element_type;
                actual = &current_ctx_->create_block_value<IRGetElemPtrInst>(
                    storage,
                    zero_value,
                    get_pointer_type(elem_type),
                    current_ctx_->next_percent_name());
            } else if (storage_type->is_pointer()) {
                IRValue* loaded_ptr = &current_ctx_->create_block_value<IRLoadInst>(
                    storage,
                    storage_type,
                    current_ctx_->next_percent_name());
                actual = loaded_ptr;
            }
        } else {
            actual = lower_lval_address(*lval_ast, true);
        }

        if (actual == nullptr) {
            semantic::throw_call_argument_type_mismatch();
        }

        if (actual->type_ == expected_ty) {
            return actual;
        }

        // this example:
        // void f1(int a[])
        // f2(int arr[][10]) { f1(arr[3]) }
        // actual->type_ is *[i32, 10], but expected_ty is *i32
        // so need the following code to transform *[i32, 10] to *i32
        if (actual->type_->is_pointer()) {
            const auto* actual_base = actual->type_->as<IRPointerType>()->base_type;
            if (actual_base->is_array()) {
                const auto* decayed_type =
                    get_pointer_type(actual_base->as<IRArrayType>()->element_type);

                if (decayed_type == expected_ty) {
                    return &current_ctx_->create_block_value<IRGetElemPtrInst>(
                        actual,
                        zero_value,
                        decayed_type,
                        current_ctx_->next_percent_name());
                }
            }
        }
    }
    throw std::runtime_error("lower_call_arg error");
}

IRValue* RewindIRBuilder::lower_primary_exp(const PrimaryExpAST& ast)
{
    return std::visit(
        overloaded{
            [&](const PrimaryExpAST::Number& number) -> IRValue* {
                return get_or_create_constant(number.value, current_ctx_->module());
            },
            [&](const PrimaryExpAST::Expression& expression) -> IRValue* {
                const auto& exp = expect_node<ExpAST>(*expression.exp, "ExpAST");
                return lower_exp(exp);
            },
            [&](const PrimaryExpAST::LValue& lvalue) -> IRValue* {
                const auto& lval_ast = expect_node<LValAST>(*lvalue.lval, "LValAST");
                return lower_lval_rvalue(lval_ast);
            }},
        ast.payload);
}

IRValue* RewindIRBuilder::lower_lval_array_address(const LValAST& ast,
                                                   IRValue* current_ptr,
                                                   const IRType* current_elem_type,
                                                   bool allow_array_decay)
{
    size_t array_dim = current_elem_type->as<IRArrayType>()->getArrayDim();
    semantic::require_array_index_count(
        ast.ident,
        array_dim,
        ast.indices.size(),
        allow_array_decay);

    // chain getelemptr for multi-dimensional array access
    for (size_t i = 0; i < ast.indices.size(); ++i) {
        const auto& exp_ast = expect_node<ExpAST>(*ast.indices[i], "ExpAST");
        auto* index = lower_exp(exp_ast);

        // determine pointer type for next level
        current_elem_type = current_elem_type->as<IRArrayType>()->element_type;

        auto& ptr = current_ctx_->create_block_value<IRGetElemPtrInst>(
            current_ptr,
            index,
            get_pointer_type(current_elem_type),
            current_ctx_->next_percent_name());
        current_ptr = &ptr;
    }

    return current_ptr;
}

IRValue* RewindIRBuilder::lower_lval_pointer_address(const LValAST& ast,
                                                     IRValue* current_ptr,
                                                     const IRType* current_elem_type,
                                                     bool allow_array_decay)
{
    IRValue* loaded_ptr = &current_ctx_->create_block_value<IRLoadInst>(
        current_ptr,
        current_elem_type,
        current_ctx_->next_percent_name());

    if (ast.indices.empty()) {
        semantic::require_array_decay_allowed(ast.ident, allow_array_decay);
        return loaded_ptr;
    }

    const auto& first_index_ast = expect_node<ExpAST>(*ast.indices[0], "ExpAST");
    auto* first_index = lower_exp(first_index_ast);
    current_elem_type = current_elem_type->as<IRPointerType>()->base_type;

    current_ptr = &current_ctx_->create_block_value<IRGetPtrInst>(
        loaded_ptr,
        first_index,
        get_pointer_type(current_elem_type),
        current_ctx_->next_percent_name());

    size_t remaining_dim =
        current_elem_type->is_array() ? current_elem_type->as<IRArrayType>()->getArrayDim() : 0;
    size_t provided_remaining = ast.indices.size() - 1;
    semantic::require_pointer_array_index_count(
        ast.ident,
        remaining_dim,
        provided_remaining,
        allow_array_decay);

    for (int i = 1; i < ast.indices.size(); i++) {
        const auto& exp_ast = expect_node<ExpAST>(*ast.indices[i], "ExpAST");
        auto* index = lower_exp(exp_ast);

        current_elem_type = current_elem_type->as<IRArrayType>()->element_type;

        auto& ptr = current_ctx_->create_block_value<IRGetElemPtrInst>(
            current_ptr,
            index,
            get_pointer_type(current_elem_type),
            current_ctx_->next_percent_name());
        current_ptr = &ptr;
    }
    return current_ptr;
}

IRValue* RewindIRBuilder::lower_lval_rvalue(const LValAST& ast)
{
    auto lval = lookup_value(ast.ident);

    // constexpr
    if (lval && std::holds_alternative<SymbolTable::Constant>(*lval)) {
        return get_or_create_constant(
            std::get<SymbolTable::Constant>(*lval).value,
            current_ctx_->module());
    }

    auto* var = lower_lval_storage_address(ast, false, false);
    return &current_ctx_->create_block_value<IRLoadInst>(
        var,
        get_i32_type(),
        current_ctx_->next_percent_name());
}

IRValue* RewindIRBuilder::lower_lval_address(const LValAST& ast, bool allow_array_decay)
{
    return lower_lval_storage_address(ast, allow_array_decay, true);
}

IRValue* RewindIRBuilder::lower_lval_storage_address(const LValAST& ast,
                                                     bool allow_array_decay,
                                                     bool require_mutable)
{
    auto lval = lookup_value(ast.ident);
    const auto var_info = require_mutable
                              ? semantic::require_mutable_variable_symbol(lval, ast.ident)
                              : semantic::require_variable_symbol(lval, ast.ident);

    auto* var = var_info.storage;

    // array
    if (is_array_storage(var)) {
        // chain getelemptr for multi-dimensional array access
        IRValue* current_ptr = var;
        const IRType* current_elem_type = get_array_storage_type(current_ptr);

        if (current_elem_type->is_array()) {
            current_ptr =
                lower_lval_array_address(ast, current_ptr, current_elem_type, allow_array_decay);
        } else if (current_elem_type->is_pointer()) {
            current_ptr = lower_lval_pointer_address(
                ast,
                current_ptr,
                current_elem_type,
                allow_array_decay);
        }
        return current_ptr;
    }

    // scalar
    semantic::require_scalar_without_indices(ast.ident, !ast.indices.empty());

    return var;
}

} // namespace rewind_ir
