#include "frame_layout.h"
#include <algorithm>
#include <stdexcept>

namespace riscv
{

/*
 | previous function stack frame |     high address
 | ----------------------------- |
 |       return address          |
 |       local variables         |
 |       temp values             | <-- inst result
 |       function params         |
 |       (tenth param)           |
 |       (ninth param)           |
 | ----------------------------- | <-- sp register address
 |                               |
 |                               |     low address
 * the design of stack frame
 * 1. low address part stores outgoing call arguments beyond a0-a7
 * 2. then stores local variable object slots and IR median result slots
 * 3. if this function contains a call, save ra at sp + frame_size - kWordSize
 * 4. align stack frame size to 16 bytes
 */
void FrameLayout::build(const rewind_ir::IRFunction& func)
{
    object_slots_.clear();
    value_slots_.clear();
    next_slot_offset_ = 0;
    frame_size_ = 0;
    ra_offset_ = 0;
    outgoing_arg_size_ = 0;
    has_saved_ra_ = false;
    size_t max_func_param_size = 0;
    int32_t payload_size = 0;

    // first traversal to calculate frame payload size
    for (const auto* block : func.basic_blocks_) {
        for (const auto* inst : block->insts_) {
            if (inst->kind_ == rewind_ir::IRValueKind::IR_CALL) {
                const auto* call_inst = inst->as<rewind_ir::IRCallInst>();
                has_saved_ra_ = true;
                max_func_param_size = std::max(max_func_param_size, call_inst->args_.size());
            }

            payload_size += data_layout_.stack_storage_size(*inst);
        }
    }

    const auto saved_ra_size = has_saved_ra_ ? DataLayout::WordSize : 0;
    outgoing_arg_size_ = calling_convention_.outgoing_arg_area_size(max_func_param_size);

    frame_size_ =
        data_layout_.align_stack_size(outgoing_arg_size_ + payload_size + saved_ra_size);
    if (has_saved_ra_) {
        ra_offset_ = frame_size_ - DataLayout::WordSize;
    }

    next_slot_offset_ = outgoing_arg_size_;
    // second traversal to ensure local objects and instruction results have stable slots
    for (const auto* block : func.basic_blocks_) {
        for (const auto* inst : block->insts_) {
            const auto storage_size = data_layout_.stack_storage_size(*inst);

            if (inst->kind_ == rewind_ir::IRValueKind::IR_ALLOC) {
                object_slots_.emplace(inst, next_slot_offset_);
                next_slot_offset_ += storage_size;
                continue;
            }

            if (storage_size > 0) {
                value_slots_.emplace(inst, next_slot_offset_);
                next_slot_offset_ += storage_size;
            }
        }
    }
}

int32_t FrameLayout::outgoing_arg_offset(size_t arg_index) const
{
    return calling_convention_.outgoing_arg_offset(arg_index);
}

int32_t FrameLayout::incoming_stack_arg_offset(size_t arg_index) const
{
    return calling_convention_.incoming_stack_arg_offset(arg_index, frame_size_);
}

const int32_t* FrameLayout::find_slot(
    const std::unordered_map<const rewind_ir::IRValue*, int32_t>& slots,
    const rewind_ir::IRValue* value)
{
    const auto it = slots.find(value);
    if (it == slots.end()) {
        return nullptr;
    }
    return &it->second;
}

bool FrameLayout::has_object_slot(const rewind_ir::IRValue* value) const
{
    return find_slot(object_slots_, value) != nullptr;
}

bool FrameLayout::has_value_slot(const rewind_ir::IRValue* value) const
{
    return find_slot(value_slots_, value) != nullptr;
}

int32_t FrameLayout::object_slot(const rewind_ir::IRValue* value) const
{
    if (const auto* slot = find_slot(object_slots_, value)) {
        return *slot;
    }
    throw std::runtime_error("missing object slot");
}

int32_t FrameLayout::value_slot(const rewind_ir::IRValue* value) const
{
    if (const auto* slot = find_slot(value_slots_, value)) {
        return *slot;
    }
    throw std::runtime_error("missing value slot");
}

} // namespace riscv
