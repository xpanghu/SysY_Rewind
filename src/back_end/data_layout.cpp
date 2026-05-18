#include "data_layout.h"
#include "ir_type.h"
#include <stdexcept>

namespace riscv
{

int32_t DataLayout::type_size(const rewind_ir::IRType* type) const
{
    if (type == nullptr) {
        return 0;
    }
    return static_cast<int32_t>(rewind_ir::IRTypeContext::instance().getTypeSize(type));
}

int32_t DataLayout::stack_storage_size(const rewind_ir::IRValue& value) const
{
    if (value.kind_ == rewind_ir::IRValueKind::IR_ALLOC) {
        const auto* pointer_type = value.type_->as<rewind_ir::IRPointerType>();
        if (pointer_type == nullptr) {
            throw std::runtime_error("alloc value must have pointer type");
        }
        return type_size(pointer_type->base_type);
    }

    return type_size(value.type_);
}

int32_t DataLayout::align_stack_size(int32_t value) const
{
    return (value + StackAlignment - 1) & ~(StackAlignment - 1);
}

} // namespace riscv
