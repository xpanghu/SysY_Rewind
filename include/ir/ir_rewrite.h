#pragma once

#include "rewind_ir.h"

#include <cstddef>
#include <functional>

namespace rewind_ir::ir_rewrite
{

using OperandVisitor = std::function<void(IRValue*& operand)>;

// Visits every IRValue* operand owned by a value/instruction. CFG targets are
// intentionally not operands; branch/jump edge arguments are.
void for_each_operand(IRValue& value, const OperandVisitor& visitor);

// Replaces all occurrences of old_value inside one value's operands.
bool replace_operand(IRValue& value, const IRValue* old_value, IRValue* new_value);

// Replaces all uses inside a function body and returns the number of rewritten
// operand slots.
std::size_t replace_all_uses(IRFunction& function,
                             const IRValue* old_value,
                             IRValue* new_value);

// Module-level replacement is useful for whole-module passes that also rewrite
// global initializers.
std::size_t replace_all_uses(IRModule& module,
                             const IRValue* old_value,
                             IRValue* new_value);

// Erasing only detaches the instruction from the block instruction list.
// IRModule still owns the IRValue, so existing raw pointers remain stable and
// no immediate memory reclamation happens during a pass.
bool erase_instruction(IRBasicBlock& block, const IRValue* instruction);

bool erase_instruction(IRBasicBlock& block, IRValue& instruction);

} // namespace rewind_ir::ir_rewrite
