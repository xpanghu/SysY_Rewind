// rewind_ir_type.h
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace rewind_ir
{

// type tag
enum class IRTypeTag {
    INT32,
    UNIT,
    ARRAY,
    POINTER,
    FUNCTION,
};

class IRType
{
public:
    const IRTypeTag tag;

    explicit IRType(IRTypeTag t) : tag(t)
    {
    }

    virtual ~IRType() = default;

    // 类型谓词
    bool is_int32() const
    { return tag == IRTypeTag::INT32; }

    bool is_unit() const
    { return tag == IRTypeTag::UNIT; }

    bool is_array() const
    { return tag == IRTypeTag::ARRAY; }

    bool is_pointer() const
    { return tag == IRTypeTag::POINTER; }

    bool is_function() const
    { return tag == IRTypeTag::FUNCTION; }

    // assertions can be added, the function list can include expected types.
    // assert(tag, expected_tag)
    template <typename T>
    const T* as() const
    {
        return static_cast<const T*>(this);
    }

    template <typename T>
    T* as()
    {
        return static_cast<T*>(this);
    }
};

// INT32
class IRInt32Type : public IRType
{
public:
    IRInt32Type() : IRType(IRTypeTag::INT32)
    {
    }
};

// UNIT
class IRUnitType : public IRType
{
public:
    IRUnitType() : IRType(IRTypeTag::UNIT)
    {
    }
};

// ARRAY
class IRArrayType : public IRType
{
public:
    const IRType* element_type;
    const size_t length;

    IRArrayType(const IRType* elem, size_t len) :
        IRType(IRTypeTag::ARRAY),
        element_type(elem),
        length(len)
    {
    }
};

// POINTER
class IRPointerType : public IRType
{
public:
    const IRType* base_type;

    IRPointerType(const IRType* base) :
        IRType(IRTypeTag::POINTER),
        base_type(base)
    {
    }
};

// FUNCTION
class IRFunctionType : public IRType
{
public:
    const std::vector<const IRType*> params;
    const IRType* return_type;

    IRFunctionType(std::vector<const IRType*> p, const IRType* ret) :
        IRType(IRTypeTag::FUNCTION),
        params(std::move(p)),
        return_type(ret)
    {
    }
};

// Type context
// singleton Pattern
class IRTypeContext
{
public:
    static IRTypeContext& instance();

    // factory function - return the singleton instance of the type
    const IRInt32Type* getInt32();
    const IRUnitType* getUnit();
    const IRArrayType* getArray(const IRType* elem, size_t len);
    const IRPointerType* getPointer(const IRType* base);
    const IRFunctionType* getFunction(std::vector<const IRType*> params, const IRType* ret);

    /*
     * target-dependent type lowering / data layout
     * Map the "IRType" to the representation on the specific target machine
     * (size, alignment, layout, register/stack passing method)
     */
    size_t getTypeSize(const IRType* type) const;
    size_t getTypeAlign(const IRType* type) const;

    // prohibit copy
    IRTypeContext(const IRTypeContext&) = delete;
    IRTypeContext& operator=(const IRTypeContext&) = delete;

private:
    IRTypeContext() = default;
    ~IRTypeContext() = default;

    // built-in type
    IRInt32Type int32_type_;
    IRUnitType unit_type_;

    // ? consider use unordered_map to replace map
    std::map<std::pair<const IRType*, size_t>, std::unique_ptr<IRArrayType>> array_types_;
    std::map<const IRType*, std::unique_ptr<IRPointerType>> pointer_types_;
    std::map<size_t, std::unique_ptr<IRFunctionType>> function_types_;
};

} // namespace rewind_ir
