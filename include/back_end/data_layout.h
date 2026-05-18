#pragma once

#include "rewind_ir.h"
#include <cstdint>

namespace riscv
{

class DataLayout
{
public:
    static constexpr int32_t WordSize = 4;
    static constexpr int32_t StackAlignment = 16;

    int32_t type_size(const rewind_ir::IRType* type) const;
    int32_t stack_storage_size(const rewind_ir::IRValue& value) const;
    int32_t align_stack_size(int32_t value) const;
};

} // namespace riscv
