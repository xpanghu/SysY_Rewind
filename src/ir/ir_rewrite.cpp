#include "ir_rewrite.h"

#include <algorithm>

namespace rewind_ir::ir_rewrite
{

namespace
{

void visit_operand(IRValue*& operand, const OperandVisitor& visitor)
{
    if (operand != nullptr) {
        visitor(operand);
    }
}

void visit_operands(std::vector<IRValue*>& operands, const OperandVisitor& visitor)
{
    for (auto*& operand : operands) {
        visit_operand(operand, visitor);
    }
}

} // namespace

void for_each_operand(IRValue& value, const OperandVisitor& visitor)
{
    switch (value.kind_) {
    case IRValueKind::IR_AGGREGATE: {
        visit_operands(value.as<IRAggregate>()->elems_, visitor);
        break;
    }
    case IRValueKind::IR_CALL: {
        visit_operands(value.as<IRCallInst>()->args_, visitor);
        break;
    }
    case IRValueKind::IR_STORE: {
        auto* store = value.as<IRStoreInst>();
        visit_operand(store->value_, visitor);
        visit_operand(store->dest_, visitor);
        break;
    }
    case IRValueKind::IR_LOAD: {
        visit_operand(value.as<IRLoadInst>()->src_, visitor);
        break;
    }
    case IRValueKind::IR_GET_PTR: {
        auto* get_ptr = value.as<IRGetPtrInst>();
        visit_operand(get_ptr->src_, visitor);
        visit_operand(get_ptr->index_, visitor);
        break;
    }
    case IRValueKind::IR_GET_ELEM_PTR: {
        auto* get_elem_ptr = value.as<IRGetElemPtrInst>();
        visit_operand(get_elem_ptr->src_, visitor);
        visit_operand(get_elem_ptr->index_, visitor);
        break;
    }
    case IRValueKind::IR_GLOBALALLOC: {
        visit_operand(value.as<IRGlobalAllocInst>()->init_, visitor);
        break;
    }
    case IRValueKind::IR_RETURN: {
        visit_operand(value.as<IRReturnInst>()->dst_, visitor);
        break;
    }
    case IRValueKind::IR_BINARY: {
        auto* binary = value.as<IRBinaryInst>();
        visit_operand(binary->lhs_, visitor);
        visit_operand(binary->rhs_, visitor);
        break;
    }
    case IRValueKind::IR_BRANCH: {
        auto* branch = value.as<IRBranchInst>();
        visit_operand(branch->cond_, visitor);
        visit_operands(branch->if_args_, visitor);
        visit_operands(branch->else_args_, visitor);
        break;
    }
    case IRValueKind::IR_JUMP: {
        visit_operands(value.as<IRJumpInst>()->args_, visitor);
        break;
    }
    case IRValueKind::IR_INTEGER:
    case IRValueKind::IR_ALLOC:
    case IRValueKind::IR_UNDEF:
    case IRValueKind::IR_ZERO_INIT:
    case IRValueKind::FUNC_ARG_REF:
    case IRValueKind::BLOCK_ARG_REF:
        break;
    }
}

bool replace_operand(IRValue& value, const IRValue* old_value, IRValue* new_value)
{
    bool changed = false;
    for_each_operand(value, [&](IRValue*& operand) {
        if (operand == old_value) {
            operand = new_value;
            changed = true;
        }
    });
    return changed;
}

std::size_t replace_all_uses(IRFunction& function,
                             const IRValue* old_value,
                             IRValue* new_value)
{
    std::size_t replacements = 0;

    for (auto* block : function.basic_blocks_) {
        for (auto* inst : block->insts_) {
            for_each_operand(*inst, [&](IRValue*& operand) {
                if (operand == old_value) {
                    operand = new_value;
                    ++replacements;
                }
            });
        }
    }

    return replacements;
}

std::size_t replace_all_uses(IRModule& module,
                             const IRValue* old_value,
                             IRValue* new_value)
{
    std::size_t replacements = 0;

    for (auto* global_value : module.global_values_) {
        for_each_operand(*global_value, [&](IRValue*& operand) {
            if (operand == old_value) {
                operand = new_value;
                ++replacements;
            }
        });
    }

    for (auto* function : module.funcs_) {
        replacements += replace_all_uses(*function, old_value, new_value);
    }

    return replacements;
}

bool erase_instruction(IRBasicBlock& block, const IRValue* instruction)
{
    const auto it = std::find(block.insts_.begin(), block.insts_.end(), instruction);
    if (it == block.insts_.end()) {
        return false;
    }

    block.insts_.erase(it);
    return true;
}

bool erase_instruction(IRBasicBlock& block, IRValue& instruction)
{
    return erase_instruction(block, &instruction);
}

} // namespace rewind_ir::ir_rewrite
