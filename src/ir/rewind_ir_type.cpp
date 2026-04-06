#include "rewind_ir_type.h"
#include <functional>
#include <stdexcept>

namespace rewind_ir
{

// Singleton instance
IRTypeContext& IRTypeContext::instance()
{
    static IRTypeContext ctx;
    return ctx;
}

// built-in type
const IRInt32Type* IRTypeContext::getInt32()
{
    return &int32_type_;
}

const IRUnitType* IRTypeContext::getUnit()
{
    return &unit_type_;
}

// key = std::pair(base_type, len)
// same elem and len will return same IRArrayType*
const IRArrayType* IRTypeContext::getArray(const IRType* elem, size_t len)
{
    if (elem == nullptr) {
        throw std::runtime_error("array element type cannot be null");
    }

    auto key = std::make_pair(elem, len);
    auto it = array_types_.find(key);
    if (it != array_types_.end()) {
        return it->second.get();
    }

    auto type = std::make_unique<IRArrayType>(elem, len);
    auto* ptr = type.get();
    array_types_[key] = std::move(type);

    return ptr;
}

// key = base_type
// same base will return same IRPointerType*
const IRPointerType* IRTypeContext::getPointer(const IRType* base)
{
    if (base == nullptr) {
        throw std::runtime_error("pointer base type cannot be null");
    }

    auto it = pointer_types_.find(base);
    if (it != pointer_types_.end()) {
        return it->second.get();
    }

    auto type = std::make_unique<IRPointerType>(base);

    auto* ptr = type.get();
    pointer_types_[base] = std::move(type);
    return ptr;
}

// key = hash(params + ret)，
// upgrade: key = std::pair<std::vector<const IRType*> params, const IRType* ret>
// same params and ret will return same IRFunctionType*
const IRFunctionType* IRTypeContext::getFunction(std::vector<const IRType*> params, const IRType* ret)
{
    if (ret == nullptr) {
        throw std::runtime_error("function return type cannot be null");
    }

    // 计算 hash 作为 key
    size_t hash = 0;
    hash ^= std::hash<size_t>()(reinterpret_cast<size_t>(ret) << 1);
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i] == nullptr) {
            throw std::runtime_error("function parameter type cannot be null");
        }
        hash ^= std::hash<size_t>()(reinterpret_cast<size_t>(params[i]) << (i + 1));
    }

    auto it = function_types_.find(hash);
    if (it != function_types_.end()) {
        return it->second.get();
    }

    auto type = std::make_unique<IRFunctionType>(std::move(params), ret);

    auto* ptr = type.get();
    function_types_[hash] = std::move(type);
    return ptr;
}

// getTypeSzie and getTypeAlign compose DataLayout
// bridge from IR to backend
size_t IRTypeContext::getTypeSize(const IRType* type) const
{
    if (type == nullptr) {
        return 0;
    }

    switch (type->tag) {
    case IRTypeTag::INT32:
        return 4; // 32-bit = 4 bytes
    case IRTypeTag::UNIT:
        return 0; // void has no size
    case IRTypeTag::POINTER:
        return 4; // RISC-V 32-bit: pointer = 4 bytes
    case IRTypeTag::ARRAY: {
        if (type->is_array()) {
            auto* array_type = type->as<IRArrayType>();
            size_t elem_size = getTypeSize(array_type->element_type);
            return elem_size * array_type->length;
        } else {
            throw std::runtime_error("not ARRAY IR Type");
        }
    }
    case IRTypeTag::FUNCTION:
        return 0; // function type has no size
    }

    return 0;
}

size_t IRTypeContext::getTypeAlign(const IRType* type) const
{
    if (type == nullptr) {
        return 4; // default alignment
    }

    switch (type->tag) {
    case IRTypeTag::INT32:
        return 4; // 32-bit aligned
    case IRTypeTag::UNIT:
        return 1;
    case IRTypeTag::POINTER:
        return 4; // RISC-V 32-bit: pointer aligned
    case IRTypeTag::ARRAY: {
        if (type->is_array()) {
            auto* array_type = type->as<IRArrayType>();
            return getTypeAlign(array_type);
        } else {
            throw std::runtime_error("not ARRAY IR Type");
        }
    }
    case IRTypeTag::FUNCTION:
        return 4;
    }

    return 4;
}

} // namespace rewind_ir
