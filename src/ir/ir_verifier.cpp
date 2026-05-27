#include "cfg_analysis.h"
#include "ir_verifier.h"
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace rewind_ir
{
namespace
{

std::string type_name(const IRType* type)
{
    if (type == nullptr) {
        return "<null>";
    }

    switch (type->tag) {
    case IRTypeTag::INT32:
        return "i32";
    case IRTypeTag::UNIT:
        return "unit";
    case IRTypeTag::POINTER:
        return "*" + type_name(type->as<IRPointerType>()->base_type);
    case IRTypeTag::ARRAY: {
        const auto* array = type->as<IRArrayType>();
        return "[" + type_name(array->element_type) + ", " + std::to_string(array->length) + "]";
    }
    case IRTypeTag::FUNCTION:
        return "function";
    }

    return "<unknown>";
}

std::string value_name(const IRValue* value)
{
    if (value == nullptr) {
        return "<null>";
    }

    if (!value->name_.empty()) {
        return value->name_;
    }

    return "<kind " + std::to_string(static_cast<int>(value->kind_)) + ">";
}

bool same_type(const IRType* lhs, const IRType* rhs)
{
    return lhs == rhs;
}

bool is_terminator(const IRValue* value)
{
    if (value == nullptr) {
        return false;
    }

    return value->kind_ == IRValueKind::IR_RETURN
        || value->kind_ == IRValueKind::IR_BRANCH
        || value->kind_ == IRValueKind::IR_JUMP;
}

const IRType* pointer_base_type(const IRValue* value)
{
    if (value == nullptr || value->type_ == nullptr || !value->type_->is_pointer()) {
        return nullptr;
    }

    return value->type_->as<IRPointerType>()->base_type;
}

bool is_i32_value(const IRValue* value)
{
    return value != nullptr && value->type_ != nullptr && value->type_->is_int32();
}

bool is_unit_type(const IRType* type)
{
    return type != nullptr && type->is_unit();
}

} // namespace

void IRVerifier::error(std::string message)
{
    errors_.push_back(std::move(message));
}

std::string IRVerifier::report() const
{
    std::ostringstream oss;
    for (const auto& item : errors_) {
        oss << item << "\n";
    }
    return oss.str();
}

bool IRVerifier::verify(const IRModule& module)
{
    errors_.clear();

    if (module.funcs_.empty()) {
        error("module has no functions");
    }

    for (const auto* global : module.global_values_) {
        if (global == nullptr) {
            error("module contains null global value");
            continue;
        }

        if (global->kind_ != IRValueKind::IR_GLOBALALLOC) {
            error("global value " + value_name(global) + " is not global alloc");
            continue;
        }

        const auto* global_alloc = global->as<IRGlobalAllocInst>();
        const auto* base = pointer_base_type(global_alloc);
        if (base == nullptr) {
            error("global alloc " + value_name(global_alloc) + " type must be pointer");
            continue;
        }

        if (global_alloc->init_ == nullptr) {
            error("global alloc " + value_name(global_alloc) + " has null initializer");
            continue;
        }

        if (!same_type(global_alloc->init_->type_, base)) {
            error("global alloc " + value_name(global_alloc)
                  + " initializer type mismatch, expected " + type_name(base)
                  + ", found " + type_name(global_alloc->init_->type_));
        }
    }

    std::unordered_set<const IRFunction*> functions;
    for (const auto* function : module.funcs_) {
        if (function == nullptr) {
            error("module contains null function");
            continue;
        }

        if (!functions.insert(function).second) {
            error("function @" + function->name_ + " is listed more than once");
        }

        if (function->type_ == nullptr) {
            error("function @" + function->name_ + " has null function type");
            continue;
        }

        if (function->params_.size() != function->type_->params.size()) {
            error("function @" + function->name_ + " parameter count mismatch, expected "
                  + std::to_string(function->type_->params.size()) + ", found "
                  + std::to_string(function->params_.size()));
        }

        for (size_t i = 0; i < function->params_.size() && i < function->type_->params.size(); ++i) {
            const auto* param = function->params_[i];
            if (param == nullptr) {
                error("function @" + function->name_ + " contains null parameter");
                continue;
            }

            if (param->kind_ != IRValueKind::FUNC_ARG_REF) {
                error("function @" + function->name_ + " parameter " + value_name(param)
                      + " is not FUNC_ARG_REF");
            }

            if (!same_type(param->type_, function->type_->params[i])) {
                error("function @" + function->name_ + " parameter " + std::to_string(i)
                      + " type mismatch, expected " + type_name(function->type_->params[i])
                      + ", found " + type_name(param->type_));
            }

            if (param->kind_ == IRValueKind::FUNC_ARG_REF
                && param->as<IRFuncArgRef>()->index_ != i) {
                error("function @" + function->name_ + " parameter " + std::to_string(i)
                      + " has wrong index " + std::to_string(param->as<IRFuncArgRef>()->index_));
            }
        }

        if (function->is_declaration_) {
            if (!function->basic_blocks_.empty()) {
                error("function declaration @" + function->name_ + " should not have basic blocks");
            }
            continue;
        }

        if (function->basic_blocks_.empty()) {
            error("function @" + function->name_ + " has no basic blocks");
            continue;
        }

        std::unordered_set<const IRBasicBlock*> blocks;
        for (const auto* block : function->basic_blocks_) {
            if (block == nullptr) {
                error("function @" + function->name_ + " contains null basic block");
                continue;
            }

            if (!blocks.insert(block).second) {
                error("basic block " + block->name_ + " is listed more than once in @"
                      + function->name_);
            }
        }

        if (!function->basic_blocks_.empty()
            && function->basic_blocks_.front() != nullptr
            && !function->basic_blocks_.front()->params_.empty()) {
            error("entry block " + function->basic_blocks_.front()->name_
                  + " in @" + function->name_ + " should not have block arguments");
        }

        for (const auto* block : function->basic_blocks_) {
            if (block == nullptr) {
                continue;
            }

            for (size_t i = 0; i < block->params_.size(); ++i) {
                const auto* param = block->params_[i];
                if (param == nullptr) {
                    error("basic block " + block->name_ + " in @" + function->name_
                          + " contains null block argument");
                    continue;
                }

                if (param->kind_ != IRValueKind::BLOCK_ARG_REF) {
                    error("basic block " + block->name_ + " argument " + value_name(param)
                          + " is not BLOCK_ARG_REF");
                    continue;
                }

                if (param->type_ == nullptr) {
                    error("basic block " + block->name_ + " argument " + value_name(param)
                          + " has null type");
                }

                const auto* block_arg = param->as<IRBlockArgRef>();
                if (block_arg->owner_ != block) {
                    error("basic block " + block->name_ + " argument " + value_name(param)
                          + " has wrong owner");
                }

                if (block_arg->index_ != i) {
                    error("basic block " + block->name_ + " argument " + value_name(param)
                          + " has wrong index "
                          + std::to_string(block_arg->index_));
                }
            }
        }

        CFGAnalysis cfg(*function);
        const auto verify_edge_arguments =
            [this, &cfg, function](const IRBasicBlock& source,
                                   const IRBasicBlock* target,
                                   const std::vector<IRValue*>& args,
                                   const std::string& edge_name) {
                if (target == nullptr || !cfg.contains(*target)) {
                    return;
                }

                if (args.size() != target->params_.size()) {
                    error(edge_name + " from " + source.name_ + " to " + target->name_
                          + " argument count mismatch, expected "
                          + std::to_string(target->params_.size()) + ", found "
                          + std::to_string(args.size()) + " in @" + function->name_);
                }

                for (size_t i = 0; i < args.size() && i < target->params_.size(); ++i) {
                    const auto* arg = args[i];
                    const auto* param = target->params_[i];
                    if (arg == nullptr) {
                        error(edge_name + " from " + source.name_ + " to " + target->name_
                              + " has null argument");
                        continue;
                    }
                    if (param == nullptr) {
                        continue;
                    }
                    if (!same_type(arg->type_, param->type_)) {
                        error(edge_name + " from " + source.name_ + " to " + target->name_
                              + " argument " + std::to_string(i)
                              + " type mismatch, expected " + type_name(param->type_)
                              + ", found " + type_name(arg->type_));
                    }
                }
            };

        for (const auto* block : function->basic_blocks_) {
            if (block == nullptr) {
                continue;
            }

            if (block->insts_.empty()) {
                error("basic block " + block->name_ + " in @" + function->name_
                      + " is missing terminator");
                continue;
            }

            for (size_t i = 0; i < block->insts_.size(); ++i) {
                const auto* inst = block->insts_[i];
                if (inst == nullptr) {
                    error("basic block " + block->name_ + " in @" + function->name_
                          + " contains null instruction");
                    continue;
                }

                if (is_terminator(inst) && i + 1 != block->insts_.size()) {
                    error("terminator appears before the end of block " + block->name_
                          + " in @" + function->name_);
                }

                switch (inst->kind_) {
                case IRValueKind::IR_INTEGER:
                    if (!is_i32_value(inst)) {
                        error("integer constant " + value_name(inst) + " must have i32 type");
                    }
                    break;
                case IRValueKind::IR_BINARY: {
                    const auto* binary = inst->as<IRBinaryInst>();
                    if (!is_i32_value(binary->lhs_) || !is_i32_value(binary->rhs_)) {
                        error("binary instruction " + value_name(binary)
                              + " operands must be i32");
                    }
                    if (!is_i32_value(binary)) {
                        error("binary instruction " + value_name(binary) + " result must be i32");
                    }
                    break;
                }
                case IRValueKind::IR_ALLOC: {
                    if (pointer_base_type(inst) == nullptr) {
                        error("alloc instruction " + value_name(inst) + " type must be pointer");
                    }
                    break;
                }
                case IRValueKind::IR_LOAD: {
                    const auto* load = inst->as<IRLoadInst>();
                    const auto* base = pointer_base_type(load->src_);
                    if (base == nullptr) {
                        error("load instruction " + value_name(load) + " source must be pointer");
                        break;
                    }
                    if (!same_type(load->type_, base)) {
                        error("load instruction " + value_name(load)
                              + " result type mismatch, expected " + type_name(base)
                              + ", found " + type_name(load->type_));
                    }
                    break;
                }
                case IRValueKind::IR_STORE: {
                    const auto* store = inst->as<IRStoreInst>();
                    const auto* base = pointer_base_type(store->dest_);
                    if (base == nullptr) {
                        error("store destination " + value_name(store->dest_) + " must be pointer");
                        break;
                    }
                    if (store->value_ == nullptr) {
                        error("store instruction has null value");
                        break;
                    }
                    if (!same_type(store->value_->type_, base)) {
                        error("store value type mismatch for destination " + value_name(store->dest_)
                              + ", expected " + type_name(base)
                              + ", found " + type_name(store->value_->type_));
                    }
                    if (!is_unit_type(store->type_)) {
                        error("store instruction must have unit type");
                    }
                    break;
                }
                case IRValueKind::IR_GET_PTR: {
                    const auto* get_ptr = inst->as<IRGetPtrInst>();
                    const auto* base = pointer_base_type(get_ptr->src_);
                    if (base == nullptr) {
                        error("getptr " + value_name(get_ptr) + " source must be pointer");
                    }
                    if (!is_i32_value(get_ptr->index_)) {
                        error("getptr " + value_name(get_ptr) + " index must be i32");
                    }
                    if (base != nullptr && !same_type(get_ptr->type_, get_ptr->src_->type_)) {
                        error("getptr " + value_name(get_ptr)
                              + " result type mismatch, expected " + type_name(get_ptr->src_->type_)
                              + ", found " + type_name(get_ptr->type_));
                    }
                    break;
                }
                case IRValueKind::IR_GET_ELEM_PTR: {
                    const auto* get_elem_ptr = inst->as<IRGetElemPtrInst>();
                    const auto* base = pointer_base_type(get_elem_ptr->src_);
                    if (base == nullptr || !base->is_array()) {
                        error("getelemptr " + value_name(get_elem_ptr)
                              + " source must be pointer to array");
                        break;
                    }
                    if (!is_i32_value(get_elem_ptr->index_)) {
                        error("getelemptr " + value_name(get_elem_ptr) + " index must be i32");
                    }
                    const auto* expected =
                        IRTypeContext::instance().getPointer(base->as<IRArrayType>()->element_type);
                    if (!same_type(get_elem_ptr->type_, expected)) {
                        error("getelemptr " + value_name(get_elem_ptr)
                              + " result type mismatch, expected " + type_name(expected)
                              + ", found " + type_name(get_elem_ptr->type_));
                    }
                    break;
                }
                case IRValueKind::IR_CALL: {
                    const auto* call = inst->as<IRCallInst>();
                    if (call->callee_ == nullptr || call->callee_->type_ == nullptr) {
                        error("call " + value_name(call) + " has null callee");
                        break;
                    }
                    if (call->args_.size() != call->callee_->type_->params.size()) {
                        error("call @" + call->callee_->name_ + " argument count mismatch");
                    }
                    for (size_t i = 0; i < call->args_.size()
                         && i < call->callee_->type_->params.size(); ++i) {
                        if (call->args_[i] == nullptr) {
                            error("call @" + call->callee_->name_ + " has null argument");
                            continue;
                        }
                        if (!same_type(call->args_[i]->type_, call->callee_->type_->params[i])) {
                            error("call @" + call->callee_->name_ + " argument "
                                  + std::to_string(i) + " type mismatch, expected "
                                  + type_name(call->callee_->type_->params[i])
                                  + ", found " + type_name(call->args_[i]->type_));
                        }
                    }
                    if (!same_type(call->type_, call->callee_->type_->return_type)) {
                        error("call @" + call->callee_->name_ + " result type mismatch, expected "
                              + type_name(call->callee_->type_->return_type)
                              + ", found " + type_name(call->type_));
                    }
                    break;
                }
                case IRValueKind::IR_BRANCH: {
                    const auto* branch = inst->as<IRBranchInst>();
                    if (!is_i32_value(branch->cond_)) {
                        error("branch condition in " + block->name_ + " must be i32");
                    }
                    if (branch->if_basic_block_ == nullptr
                        || !cfg.contains(*branch->if_basic_block_)) {
                        error("branch true target in " + block->name_
                              + " does not belong to function @" + function->name_);
                    }
                    if (branch->else_basic_block_ == nullptr
                        || !cfg.contains(*branch->else_basic_block_)) {
                        error("branch false target in " + block->name_
                              + " does not belong to function @" + function->name_);
                    }
                    verify_edge_arguments(*block,
                                          branch->if_basic_block_,
                                          branch->if_args_,
                                          "branch true edge");
                    verify_edge_arguments(*block,
                                          branch->else_basic_block_,
                                          branch->else_args_,
                                          "branch false edge");
                    if (!is_unit_type(branch->type_)) {
                        error("branch instruction must have unit type");
                    }
                    break;
                }
                case IRValueKind::IR_JUMP: {
                    const auto* jump = inst->as<IRJumpInst>();
                    if (jump->jump_basic_block_ == nullptr
                        || !cfg.contains(*jump->jump_basic_block_)) {
                        error("jump target in " + block->name_
                              + " does not belong to function @" + function->name_);
                    }
                    verify_edge_arguments(*block,
                                          jump->jump_basic_block_,
                                          jump->args_,
                                          "jump edge");
                    if (!is_unit_type(jump->type_)) {
                        error("jump instruction must have unit type");
                    }
                    break;
                }
                case IRValueKind::IR_RETURN: {
                    const auto* ret = inst->as<IRReturnInst>();
                    const auto* return_type = function->type_->return_type;
                    if (return_type->is_unit()) {
                        if (ret->dst_ != nullptr) {
                            error("void function @" + function->name_
                                  + " returns a value of type " + type_name(ret->dst_->type_));
                        }
                    } else {
                        if (ret->dst_ == nullptr) {
                            error("non-void function @" + function->name_ + " has empty return");
                        } else if (!same_type(ret->dst_->type_, return_type)) {
                            error("function @" + function->name_
                                  + " return type mismatch, expected " + type_name(return_type)
                                  + ", found " + type_name(ret->dst_->type_));
                        }
                    }
                    break;
                }
                case IRValueKind::IR_GLOBALALLOC:
                case IRValueKind::IR_UNDEF:
                case IRValueKind::IR_ZERO_INIT:
                case IRValueKind::IR_AGGREGATE:
                case IRValueKind::FUNC_ARG_REF:
                case IRValueKind::BLOCK_ARG_REF:
                    error("value " + value_name(inst) + " of kind "
                          + std::to_string(static_cast<int>(inst->kind_))
                          + " should not appear as a block instruction");
                    break;
                }

                if (i + 1 == block->insts_.size() && !is_terminator(inst)) {
                    error("basic block " + block->name_ + " in @" + function->name_
                          + " is missing terminator");
                }
            }
        }
    }

    return errors_.empty();
}

void verify_or_throw(const IRModule& module)
{
    IRVerifier verifier;
    if (!verifier.verify(module)) {
        throw std::runtime_error("IR verification failed:\n" + verifier.report());
    }
}

} // namespace rewind_ir
