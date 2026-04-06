#pragma once

#include "rewind_ir_type.h"
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <stdexcept>

// AST 相关类（前向声明）
class BaseAST;
class CompUnitAST;
class FuncDefAST;
class FuncTypeAST;
class BlockAST;
class StmtAST;
class ExpAST;
class LOrExpAST;
class LAndExpAST;
class EqExpAST;
class RelExpAST;
class AddExpAST;
class MulExpAST;
class UnaryExpAST;
class PrimaryExpAST;
class DeclAST;
class ConstDeclAST;
class ConstDefAST;
class ConstInitValAST;
class LValAST;

namespace rewind_ir
{

enum class IRValueKind {
    IR_INTEGER,      // 整数常量
    IR_RETURN,       // 返回指令
    IR_BINARY,       // 二元运算
    IR_ALLOC,        // 栈分配（局部变量）
    IR_STORE,        // 存储指令
    IR_LOAD,         // 加载指令
    IR_GLOBALALLOC,  // 全局分配
    IR_JUMP,         // 无条件跳转
    IR_BRANCH,       // 条件分支
    IR_CALL,         // 函数调用
    IR_GET_PTR,      // 指针计算
    IR_GET_ELEM_PTR, // 元素指针计算
    IR_UNDEF,        // 未定义值
    IR_ZERO_INIT,    // 零初始化
    IR_AGGREGATE,    // 聚合常量
    FUNC_ARG_REF,    // 函数参数引用
    BLOCK_ARG_REF    // 基本块参数引用
};

enum class IRBinaryOp {
    MUL,
    DIV,
    MOD,
    ADD,
    SUB,
    EQ,
    NEQ,
    AND,
    OR,
    XOR,
    LT,
    GT,
    LE,
    GE,
    SHL,
    SHR,
    SAR,
};

class IRModule;
class IRFunction;
class IRBasicBlock;
class IRValue;
class IRInstruction;

// ========= IRValue =========
class IRValue
{
    friend class IRModule;

public:
    std::string name_;
    IRValueKind kind_;
    const IRType* type_;

    virtual ~IRValue() = default;

    template <typename T>
    T* as()
    { return static_cast<T*>(this); }

    template <typename T>
    const T* as() const
    { return static_cast<const T*>(this); }

    // 类型谓词
    bool is_binary() const
    { return kind_ == IRValueKind::IR_BINARY; }
    bool is_integer() const
    { return kind_ == IRValueKind::IR_INTEGER; }
    bool is_alloc() const
    { return kind_ == IRValueKind::IR_ALLOC; }
    bool is_store() const
    { return kind_ == IRValueKind::IR_STORE; }
    bool is_load() const
    { return kind_ == IRValueKind::IR_LOAD; }
    bool is_ret() const
    { return kind_ == IRValueKind::IR_RETURN; }
    bool is_global_alloc() const
    { return kind_ == IRValueKind::IR_GLOBALALLOC; }

protected:
    IRValue() = default;

    explicit IRValue(IRValueKind k, const IRType* ty, const std::string& name = {}) : name_(name), kind_(k), type_(ty)
    {
    }
};

// No IR generated
// name_ is empty
// type_ = int32
class IRConstant : public IRValue
{
    friend class IRModule;

public:
    int32_t value_;
    ~IRConstant() override = default;

private:
    IRConstant() = default;
    explicit IRConstant(int32_t value, const IRType* ty, const std::string& name = {}) : IRValue(IRValueKind::IR_INTEGER, ty, name), value_(value)
    {
    }
};

class IRInstruction : public IRValue
{
    friend class IRModule;

public:
    ~IRInstruction() override = default;

protected:
    IRInstruction() = default;

    explicit IRInstruction(IRValueKind k, const IRType* ty, const std::string& name = {}) : IRValue(k, ty, name)
    {
    }
};

// example: @x = alloc i32
// name_ = alloc name
// type_ = pointer<i32>
class IRAllocInst : public IRInstruction
{
    friend class IRModule;

public:
    ~IRAllocInst() override = default;

private:
    IRAllocInst() = default;
    // alloc_ty 应该是指针类型，如 pointer<i32>
    explicit IRAllocInst(const IRType* alloc_ty, const std::string& name = {}) : IRInstruction(IRValueKind::IR_ALLOC, alloc_ty, name)
    {
    }
};

// example: store value_, dest_
// name_ is empty
// type_ = unit
class IRStoreInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* value_;
    IRValue* dest_;
    ~IRStoreInst() override = default;

private:
    IRStoreInst() = default;
    explicit IRStoreInst(IRValue* value, IRValue* dest, const std::string& name = {}) : IRInstruction(IRValueKind::IR_STORE, nullptr, name), value_(value), dest_(dest)
    {
    }
};

// example: %0 = load @x
// name represent inst name(virtual register)
// type_ = int32
class IRLoadInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* src_;
    ~IRLoadInst() override = default;

private:
    IRLoadInst() = default;
    explicit IRLoadInst(IRValue* src, const IRType* ty, const std::string& name = {}) : IRInstruction(IRValueKind::IR_LOAD, ty, name), src_(src)
    {
    }
};

// example: global @name = alloc i32
// name represent alloc name
// type_ = pointer<i32>
class IRGlobalAllocInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* init_; // 初始值
    ~IRGlobalAllocInst() override = default;

private:
    IRGlobalAllocInst() = default;
    explicit IRGlobalAllocInst(IRValue* init, const std::string& name = {}) : IRInstruction(IRValueKind::IR_GLOBALALLOC, nullptr, name), init_(init)
    {
    }
};

// example: ret %0
// name is empty
// type_ = unit
class IRReturnInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* dst_; // 返回值（可以为 nullptr 表示 void 返回）
    ~IRReturnInst() override = default;

private:
    IRReturnInst() = default;
    explicit IRReturnInst(IRValue* dst, const std::string& name = {}) : IRInstruction(IRValueKind::IR_RETURN, nullptr, name), dst_(dst)
    {
    }
};

// example: %2 = add %0, %1
// name represent virtual register
// type_ = int32
class IRBinaryInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* lhs_;
    IRValue* rhs_;
    IRBinaryOp op_;
    ~IRBinaryInst() override = default;

private:
    IRBinaryInst() = default;
    explicit IRBinaryInst(IRBinaryOp op, IRValue* lhs, IRValue* rhs,
                          const IRType* ty, const std::string& name = {}) : IRInstruction(IRValueKind::IR_BINARY, ty, name), lhs_(lhs), rhs_(rhs), op_(op)
    {
    }
};

// ========== IRBasicBlock ==========
class IRBasicBlock
{
    friend class IRModule;

public:
    std::string name_;
    std::vector<IRValue*> insts_;

private:
    IRBasicBlock() = default;
    explicit IRBasicBlock(const std::string& name, std::vector<IRValue*> insts = {}) : name_(name), insts_(insts)
    {
    }
};

class IRFunction
{
    friend class IRModule;

public:
    const IRType* type_; // 函数类型（如 function<[i32], i32>）
    std::string name_;
    std::vector<IRBasicBlock*> basic_blocks_;

private:
    IRFunction() = default;
    explicit IRFunction(const IRType* type, const std::string& name) : type_(type), name_(name), basic_blocks_({})
    {
    }
};

// ========== IRModule ==========
class IRModule
{
public:
    IRModule() = default;
    IRModule(const IRModule&) = delete;
    IRModule& operator=(const IRModule&) = delete;
    IRModule(IRModule&&) noexcept = default;
    IRModule& operator=(IRModule&&) noexcept = default;

    IRFunction* make_function(const IRType* type, const std::string& name);

    IRBasicBlock* make_basic_block(const std::string& name, std::vector<IRValue*> insts = {});

    template <typename T, typename... Args>
    T* make_value(Args&&... args);

    void append_basic_block(IRFunction& function, IRBasicBlock& block);

    void append_inst(IRBasicBlock& block, IRValue& value);

    std::vector<IRFunction*> funcs_;
    std::vector<IRValue*> global_values_;

private:
    std::vector<std::unique_ptr<IRFunction>> func_storage_;
    std::vector<std::unique_ptr<IRBasicBlock>> bb_storage_;
    std::vector<std::unique_ptr<IRValue>> value_storage_;
};

inline IRFunction* IRModule::make_function(const IRType* type, const std::string& name)
{
    std::unique_ptr<IRFunction> func(new IRFunction(type, name));
    IRFunction* ptr = func.get();
    func_storage_.push_back(std::move(func));
    funcs_.push_back(ptr);
    return ptr;
}

inline IRBasicBlock* IRModule::make_basic_block(const std::string& name, std::vector<IRValue*> insts)
{
    std::unique_ptr<IRBasicBlock> bb(new IRBasicBlock(name, insts));
    IRBasicBlock* ptr = bb.get();
    bb_storage_.push_back(std::move(bb));
    return ptr;
}

template <typename T, typename... Args>
inline T* IRModule::make_value(Args&&... args)
{
    static_assert(std::is_base_of_v<IRValue, T>, "make_value<T>: T must derive from IRValue");
    std::unique_ptr<T> value(new T(std::forward<Args>(args)...));
    T* ptr = value.get();
    value_storage_.push_back(std::move(value));
    return ptr;
}

inline void IRModule::append_basic_block(IRFunction& function, IRBasicBlock& block)
{
    function.basic_blocks_.push_back(&block);
}

inline void IRModule::append_inst(IRBasicBlock& block, IRValue& value)
{
    block.insts_.push_back(&value);
}

} // namespace rewind_ir
