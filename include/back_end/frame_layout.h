#pragma once

#include "calling_convention.h"
#include "data_layout.h"
#include "rewind_ir.h"
#include <cstdint>
#include <unordered_map>

namespace riscv
{

class FrameLayout
{
public:
    void build(const rewind_ir::IRFunction& func);

    bool has_object_slot(const rewind_ir::IRValue* value) const;
    bool has_value_slot(const rewind_ir::IRValue* value) const;

    int32_t object_slot(const rewind_ir::IRValue* value) const;
    int32_t value_slot(const rewind_ir::IRValue* value) const;

    int32_t frame_size() const
    {
        return frame_size_;
    }

    int32_t ra_offset() const
    {
        return ra_offset_;
    }

    bool has_saved_ra() const
    {
        return has_saved_ra_;
    }

    int32_t outgoing_arg_size() const
    {
        return outgoing_arg_size_;
    }

    int32_t outgoing_arg_offset(size_t arg_index) const;
    int32_t incoming_stack_arg_offset(size_t arg_index) const;

private:
    static const int32_t* find_slot(
        const std::unordered_map<const rewind_ir::IRValue*, int32_t>& slots,
        const rewind_ir::IRValue* value);

    DataLayout data_layout_;
    CallingConvention calling_convention_;

    int32_t next_slot_offset_ = 0;
    int32_t frame_size_ = 0;
    int32_t ra_offset_ = 0;
    int32_t outgoing_arg_size_ = 0;
    bool has_saved_ra_ = false;

    std::unordered_map<const rewind_ir::IRValue*, int32_t> object_slots_;
    std::unordered_map<const rewind_ir::IRValue*, int32_t> value_slots_;
};

} // namespace riscv
