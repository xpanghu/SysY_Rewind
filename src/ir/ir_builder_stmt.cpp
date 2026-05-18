#include "ir_builder.h"
#include "func_context.h"
#include "ir_builder_internal.h"
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <variant>

namespace rewind_ir
{
using namespace builder_detail;

void RewindIRBuilder::lower_stmt(const StmtAST& ast)
{
    std::visit(
        overloaded{
            [&](const StmtAST::Return& ret_stmt) {
                IRValue* ret_value = nullptr;

                /*
                 * check if return exp exist
                 * then check if return exp type same as  function return type
                 */
                if (ret_stmt.exp) {
                    if (current_ctx_->current_function().type_->return_type->is_unit()) {
                        throw std::runtime_error("void function should not return a value");
                    }
                    const auto& exp = expect_node<ExpAST>(*ret_stmt.exp, "ExpAST");
                    ret_value = lower_exp(exp);
                } else if (current_ctx_->current_function().type_->return_type->is_int32()) {
                    ret_value = get_or_create_constant(0, current_ctx_->module());
                }

                static_cast<void>(current_ctx_->terminate_with_return(ret_value));
            },
            [&](const StmtAST::Assign& assign_stmt) {
                // assign

                // store exp_value, alloc
                const auto& exp = expect_node<ExpAST>(*assign_stmt.exp, "ExpAST");
                auto exp_value = lower_exp(exp);

                const auto& lval_ast = expect_node<LValAST>(*assign_stmt.lval, "LValAST");

                auto* var = lower_lval_address(lval_ast);

                static_cast<void>(current_ctx_->create_block_value<IRStoreInst>(
                    exp_value,
                    var,
                    get_unit_type()));
            },
            [&](const StmtAST::Block& block_stmt) {
                const auto& block = expect_node<BlockAST>(*block_stmt.block, "BlockAST");
                lower_block(block);
            },
            [&](const StmtAST::Exp& exp_stmt) {
                // exp is empty, just return
                if (!exp_stmt.exp) {
                    return;
                }
                const auto& exp = expect_node<ExpAST>(*exp_stmt.exp, "ExpAST");
                static_cast<void>(lower_exp(exp));
            },
            [&](const StmtAST::SelectStmt& select_stmt) {
                // if ( cond ) if_stmt else else_stmt , else_stmt may be empty
                const auto& exp = expect_node<ExpAST>(*select_stmt.exp, "ExpAST");
                const auto& if_stmt = expect_node<StmtAST>(*select_stmt.if_stmt, "StmtAST");
                const StmtAST* else_stmt =
                    select_stmt.else_stmt
                        ? &expect_node<StmtAST>(*select_stmt.else_stmt, "StmtAST")
                        : nullptr;

                // condition value
                auto* cond = lower_exp(exp);

                // def if_basic_block, else_basic_block  merge_basic_block
                auto& then_bb = current_ctx_->create_function_block("then");
                IRBasicBlock* else_bb = nullptr;
                if (else_stmt != nullptr) {
                    else_bb = &current_ctx_->create_function_block("else");
                }

                IRBasicBlock* merge_bb = nullptr;
                if (else_stmt == nullptr) {
                    merge_bb = &current_ctx_->create_function_block("end");
                }

                // current_block add branch inst
                static_cast<void>(current_ctx_->terminate_with_branch(
                    cond,
                    then_bb,
                    *(else_stmt != nullptr ? else_bb : merge_bb)));

                // switch then_bb
                current_ctx_->set_current_block(then_bb);
                lower_stmt(if_stmt);
                IRBasicBlock* then_fallthrough = current_ctx_->current_block_or_null();

                // check if then_bb terminated
                // consider example : if ( exp ) return exp;  don't need jump inst
                if (then_fallthrough != nullptr) {
                    if (merge_bb == nullptr) {
                        merge_bb = &current_ctx_->create_function_block("end");
                    }
                    current_ctx_->set_current_block(*then_fallthrough);
                    static_cast<void>(current_ctx_->terminate_with_jump(*merge_bb));
                }

                // switch else_basic_block
                if (else_stmt != nullptr) {
                    current_ctx_->set_current_block(*else_bb);
                    lower_stmt(*else_stmt);
                    IRBasicBlock* else_fallthrough = current_ctx_->current_block_or_null();

                    // check if else_basic_block terminated
                    if (else_fallthrough != nullptr) {
                        if (merge_bb == nullptr) {
                            merge_bb = &current_ctx_->create_function_block("end");
                        }
                        current_ctx_->set_current_block(*else_fallthrough);
                        static_cast<void>(current_ctx_->terminate_with_jump(*merge_bb));
                    }
                }

                // if_bb and else_bb all terminate, don't need merge_bb
                // this way can prevent emtpy merge_bb
                if (merge_bb != nullptr) {
                    current_ctx_->set_current_block(*merge_bb);
                } else {
                    current_ctx_->clear_current_block();
                }
            },
            [&](const StmtAST::LoopStmt& loop_stmt) {
                const auto& exp = expect_node<ExpAST>(*loop_stmt.exp, "ExpAST");
                const auto& body_stmt = expect_node<StmtAST>(*loop_stmt.body_stmt, "StmtAST");

                auto& while_entry = current_ctx_->create_function_block("while_entry");
                auto& while_body = current_ctx_->create_function_block("while_body");
                auto& end = current_ctx_->create_function_block("end");

                // record break and continue basic block
                current_ctx_->push_loop(end, while_entry);

                // preheader -> while_entry
                static_cast<void>(current_ctx_->terminate_with_jump(while_entry));

                // while_entry:
                //   evaluate condition
                //   br cond, while_body, end
                current_ctx_->set_current_block(while_entry);
                auto* cond = lower_exp(exp);
                static_cast<void>(current_ctx_->terminate_with_branch(
                    cond,
                    while_body,
                    end));

                // while_body:
                //   lower body
                //   if body still falls through, jump back to while_entry
                current_ctx_->set_current_block(while_body);
                lower_stmt(body_stmt);

                // check if while_body is terminated
                auto* body_fallthrough = current_ctx_->current_block_or_null();
                if (body_fallthrough != nullptr) {
                    current_ctx_->set_current_block(*body_fallthrough);
                    static_cast<void>(current_ctx_->terminate_with_jump(while_entry));
                }

                // exit loop
                current_ctx_->pop_loop();

                // end:
                //   subsequent statements continue here
                current_ctx_->set_current_block(end);
            },
            [&](const StmtAST::LoopControlStmt& control_stmt) {
                if (!current_ctx_->in_loop()) {
                    if (control_stmt.kind == StmtAST::LoopControlStmt::Kind::Break) {
                        throw std::runtime_error("break used outside while");
                    } else {
                        throw std::runtime_error("continue used outside while");
                    }
                }

                switch (control_stmt.kind) {
                case StmtAST::LoopControlStmt::Kind::Break: {
                    current_ctx_->terminate_with_jump(*current_ctx_->current_loop().break_target);
                    break;
                }
                case StmtAST::LoopControlStmt::Kind::Continue: {
                    current_ctx_->terminate_with_jump(
                        *current_ctx_->current_loop().continue_target);
                    break;
                }
                }
            },
            [&](const auto& other) {
                std::string type_name = typeid(other).name();
                throw std::runtime_error("Unsupported statement type: " + type_name);
            }},
        ast.payload);
}

} // namespace rewind_ir
