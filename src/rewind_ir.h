#pragma once

#include "koopa.h"

#include <cassert>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// AST相关类
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

namespace rewind_ir {
enum class IRValueType;
enum class IRValueKind;
enum class IRBinaryOp;
class Module;
class IRFunction;
class IRBasicBlock;
class IRValue;
class IRInstruction;
class IRGlobalVariable;

enum class IRValueType {
    POINTER,
    INT32,
    UNIT,
    ARRAY,
    FUNCTION
};

// value(统一值模型)类型
enum class IRValueKind {
    IR_INTEGER, // int类型
    IR_RETURN, // return
    IR_BINARY, // 二元运算
    IR_ALLOC,
    IR_STORE,
    IR_GLOBALALLOC,
    IR_JUMP,
    IR_BRANCH,
    IR_CALL, // 函数调用
    IR_GET_PTR,
    IR_GET_ELEM_PTR,
    IR_UNDEF,
    IR_ZERO_INIT,
    IR_AGGREGATE,
    FUNC_ARG_REF,
    BLOCK_ARG_REF
};

enum class IRBinaryOp {
    MUL,
    DIV,
    MOD,
    ADD,
    SUB,
    EQ,
    NEQ,
    AND, // 按位或
    OR, // 按位或
    XOR,
    LT,
    GT,
    LE,
    GE,
    SHL, // 逻辑左移
    SHR, // 逻辑右移
    SAR, // 算数右移
};

class IRModule {
public:
    IRModule() = default;
    IRModule(const IRModule&) = delete;
    IRModule& operator=(const IRModule&) = delete;
    IRModule(IRModule&&) noexcept = default;
    IRModule& operator=(IRModule&&) noexcept = default;

    IRFunction* make_function(IRValueType type, std::string name);
    IRBasicBlock* make_basic_block(std::string name = {});

    template <typename T, typename... Args>
    T* make_value(Args&&... args);

    void append_basic_block(IRFunction& function, IRBasicBlock& block);
    void append_inst(IRBasicBlock& block, IRValue& value);

    std::vector<IRFunction*> funcs_;
    std::vector<IRValue*> values_;

private:
    std::vector<std::unique_ptr<IRFunction>> func_storage_;
    std::vector<std::unique_ptr<IRBasicBlock>> bb_storage_;
    std::vector<std::unique_ptr<IRValue>> value_storage_;
};

class IRFunction {
    friend class IRModule;

public:
    IRValueType type_;
    std::string name_;
    std::vector<IRBasicBlock*> basic_blocks_;

private:
    IRFunction() = default;
    IRFunction(IRValueType type, std::string name)
        : type_(type)
        , name_(name)
        , basic_blocks_({})
    {
    }
};

class IRBasicBlock {
    friend class IRModule;

public:
    std::string name_;
    std::vector<IRValue*> insts_;

private:
    IRBasicBlock() = default;
    explicit IRBasicBlock(std::string name)
        : name_(name)
        , insts_({})
    {
    }
};

class IRValue {
    friend class IRModule;

public:
    std::string name;
    IRValueKind kind; // IRValue具体类型

    virtual ~IRValue() = default;

    template <typename T>
    T* as()
    {
        return static_cast<T*>(this);
    }

    template <typename T>
    const T* as() const
    {
        return static_cast<const T*>(this);
    }

    // Type predicates for common checks
    bool is_binary() const { return kind == IRValueKind::IR_BINARY; }
    bool is_integer() const { return kind == IRValueKind::IR_INTEGER; }
    bool is_alloc() const { return kind == IRValueKind::IR_ALLOC; }
    bool is_store() const { return kind == IRValueKind::IR_STORE; }
    bool is_ret() const { return kind == IRValueKind::IR_RETURN; }

protected:
    IRValue() = default;

    explicit IRValue(IRValueKind k)
        : kind(k)
    {
    }
};

class IRInstruction : public IRValue {
    friend class IRModule;

public:
    ~IRInstruction() override = default;

protected:
    IRInstruction() = default;

    explicit IRInstruction(IRValueKind k)
        : IRValue(k)
    {
    }
};

class IRConstant : public IRValue {
    friend class IRModule;

public:
    int value_;
    ~IRConstant() override = default;

private:
    IRConstant() = default;

    explicit IRConstant(int value)
        : IRValue(IRValueKind::IR_INTEGER)
        , value_(value)
    {
    }
};

class IRReturnInst : public IRInstruction {
    friend class IRModule;

public:
    IRValue* dst_;
    ~IRReturnInst() override = default;

private:
    IRReturnInst() = default;
    explicit IRReturnInst(IRValue* dst)
        : IRInstruction(IRValueKind::IR_RETURN)
        , dst_(dst)
    {
    }
};

class IRBinaryInst : public IRInstruction {
    friend class IRModule;

public:
    IRValue* lhs_;
    IRValue* rhs_;
    IRBinaryOp op_;

    ~IRBinaryInst() override = default;

private:
    IRBinaryInst() = default;
    explicit IRBinaryInst(IRBinaryOp op, IRValue* lhs = nullptr, IRValue* rhs = nullptr)
        : IRInstruction(IRValueKind::IR_BINARY)
        , lhs_(lhs)
        , rhs_(rhs)
        , op_(op)
    {
    }
};

inline IRFunction* IRModule::make_function(IRValueType type, std::string name)
{
    std::unique_ptr<IRFunction> func(new IRFunction(type, name));
    IRFunction* ptr = func.get();
    func_storage_.push_back(std::move(func));
    funcs_.push_back(ptr);
    return ptr;
}

inline IRBasicBlock* IRModule::make_basic_block(std::string name)
{
    std::unique_ptr<IRBasicBlock> bb(new IRBasicBlock(name));
    IRBasicBlock* ptr = bb.get();
    bb_storage_.push_back(std::move(bb));
    return ptr;
}

template <typename T, typename... Args>
inline T* IRModule::make_value(Args&&... args)
{
    static_assert(std::is_base_of_v<IRValue, T>,
        "make_value<T>: T must derive from IRValue");
    std::unique_ptr<T> value(new T(std::forward<Args>(args)...));
    T* ptr = value.get();
    value_storage_.push_back(std::move(value));
    values_.push_back(ptr);
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

} // namespace koopa_ir
