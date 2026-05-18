#include "calling_convention.h"
#include <stdexcept>

namespace riscv
{

bool CallingConvention::is_register_arg(size_t index) const
{
    return index < ArgRegisterCount;
}

Register CallingConvention::arg_register(size_t index) const
{
    switch (index) {
    case 0:
        return Register::a0;
    case 1:
        return Register::a1;
    case 2:
        return Register::a2;
    case 3:
        return Register::a3;
    case 4:
        return Register::a4;
    case 5:
        return Register::a5;
    case 6:
        return Register::a6;
    case 7:
        return Register::a7;
    default:
        throw std::runtime_error("too many register arguments");
    }
}

int32_t CallingConvention::outgoing_arg_area_size(size_t arg_count) const
{
    if (arg_count <= ArgRegisterCount) {
        return 0;
    }
    return static_cast<int32_t>((arg_count - ArgRegisterCount) * DataLayout::WordSize);
}

int32_t CallingConvention::outgoing_arg_offset(size_t arg_index) const
{
    if (is_register_arg(arg_index)) {
        throw std::runtime_error("outgoing_arg_offset requires stack-passed argument");
    }
    const auto stack_index = arg_index - ArgRegisterCount;
    return static_cast<int32_t>(stack_index * DataLayout::WordSize);
}

int32_t CallingConvention::incoming_stack_arg_offset(size_t arg_index, int32_t frame_size) const
{
    if (is_register_arg(arg_index)) {
        throw std::runtime_error("incoming_stack_arg_offset requires stack-passed argument");
    }
    return frame_size + outgoing_arg_offset(arg_index);
}

} // namespace riscv
