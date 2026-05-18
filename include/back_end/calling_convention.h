#pragma once

#include "asm_writer.h"
#include "data_layout.h"
#include <cstddef>
#include <cstdint>

namespace riscv
{

class CallingConvention
{
public:
    static constexpr size_t ArgRegisterCount = 8;

    bool is_register_arg(size_t index) const;
    Register arg_register(size_t index) const;
    int32_t outgoing_arg_area_size(size_t arg_count) const;
    int32_t outgoing_arg_offset(size_t arg_index) const;
    int32_t incoming_stack_arg_offset(size_t arg_index, int32_t frame_size) const;
};

} // namespace riscv
