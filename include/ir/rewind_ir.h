#pragma once

#include "ir_type.h"
#include <cassert>
#include <memory>
#include <ratio>
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
    LT, // <
    GT, // >
    LE, // <=
    GE, // >=
    SHL,
    SHR,
    SAR,
};

class IRModule;
class IRFunction;
class IRBasicBlock;
class IRValue;
class IRInstruction;

// IRValue
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

    bool is_binary() const
    {
        return kind_ == IRValueKind::IR_BINARY;
    }

    bool is_integer() const
    {
        return kind_ == IRValueKind::IR_INTEGER;
    }

    bool is_alloc() const
    {
        return kind_ == IRValueKind::IR_ALLOC;
    }

    bool is_store() const
    {
        return kind_ == IRValueKind::IR_STORE;
    }

    bool is_load() const
    {
        return kind_ == IRValueKind::IR_LOAD;
    }

    bool is_ret() const
    {
        return kind_ == IRValueKind::IR_RETURN;
    }

    bool is_global_alloc() const
    {
        return kind_ == IRValueKind::IR_GLOBALALLOC;
    }

    bool is_branch() const
    {
        return kind_ == IRValueKind::IR_BRANCH;
    }

    bool is_jump() const
    {
        return kind_ == IRValueKind::IR_JUMP;
    }

    bool is_call() const
    {
        return kind_ == IRValueKind::IR_CALL;
    }

    bool is_aggregate() const
    {
        return kind_ == IRValueKind::IR_AGGREGATE;
    }

    bool is_zero_init() const
    {
        return kind_ == IRValueKind::IR_ZERO_INIT;
    }

    bool is_func_arg_ref() const
    {
        return kind_ == IRValueKind::FUNC_ARG_REF;
    }

protected:
    IRValue() = default;

    explicit IRValue(IRValueKind k, const IRType* ty, const std::string& name = {}) : name_(name), kind_(k), type_(ty)
    {
    }
};

class IRAggregate : public IRValue
{
    friend class IRModule;

public:
    std::vector<IRValue*> elems_;
    ~IRAggregate() override = default;

private:
    explicit IRAggregate(std::vector<IRValue*> elem, const IRType* ty, const std::string& name = {}) :
        IRValue(IRValueKind::IR_AGGREGATE, ty, name),
        elems_(std::move(elem))
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
    explicit IRConstant(int32_t value, const IRType* ty, const std::string& name = {}) :
        IRValue(IRValueKind::IR_INTEGER, ty, name),
        value_(value)
    {
    }
};

// zero initializer
class IRZeroInit : public IRValue
{
    friend class IRModule;

public:
    ~IRZeroInit() override = default;

private:
    explicit IRZeroInit(const IRType* ty, const std::string& name = {}) :
        IRValue(IRValueKind::IR_ZERO_INIT, ty, name)
    {
    }
};

// function formal arg
class IRFuncArgRef : public IRValue
{
    friend class IRModule;

public:
    size_t index_; // arg position
    ~IRFuncArgRef() override = default;

private:
    IRFuncArgRef() = default;

    explicit IRFuncArgRef(size_t index, const IRType* ty, const std::string& name = {}) :
        IRValue(IRValueKind::FUNC_ARG_REF, ty, name),
        index_(index)
    {
    }
};

/*
 * instructon value
 */
class IRInstruction : public IRValue
{
    friend class IRModule;

public:
    ~IRInstruction() override = default;

protected:
    IRInstruction() = default;

    explicit IRInstruction(IRValueKind kind, const IRType* ty, const std::string& name = {}) :
        IRValue(kind, ty, name)
    {
    }
};
// FunCall :: = "call" SYMBOL "("[Value{"," Value}] ")"
// call inst type depends on SYMBOL type
// name represent inst result
class IRCallInst : public IRInstruction
{
    friend class IRModule;

public:
    IRFunction* callee_;
    std::vector<IRValue*> args_;
    ~IRCallInst() override = default;

private:
    IRCallInst() = default;

    explicit IRCallInst(IRFunction* callee, std::vector<IRValue*> args,
                        const IRType* ty, std::string name = {}) :
        IRInstruction(IRValueKind::IR_CALL, ty, name),
        callee_(callee),
        args_(std::move(args))
    {
    }
};

// MemoryDeclaration ::= "alloc" Type;
// IR name_ = the name of alloc address
// IR type_ = *Type
class IRAllocInst : public IRInstruction
{
    friend class IRModule;

public:
    ~IRAllocInst() override = default;

private:
    IRAllocInst() = default;

    explicit IRAllocInst(const IRType* ty, const std::string& name = {}) :
        IRInstruction(IRValueKind::IR_ALLOC, ty, name)
    {
    }
};

// Store :: = "store"(Value | Initializer) "," SYMBOL;
// SYMBOL type must be pointer type, set to *t
// then (Value | Initializer) type is t
// type unit
// name empty
class IRStoreInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* value_;
    IRValue* dest_;
    ~IRStoreInst() override = default;

private:
    IRStoreInst() = default;
    explicit IRStoreInst(IRValue* value, IRValue* dest, const IRType* ty, const std::string& name = {}) : IRInstruction(IRValueKind::IR_STORE, ty, name), value_(value), dest_(dest)
    {
    }
};

//  Load ::= "load" SYMBOL;
//  SYMBOL type_ must be pointer_type, set to *t,
//  then the return type of load is t
//  name represent inst result
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

// GetPointer ::= "getptr" SYMBOL "," Value;
// SYMBOL is a pointer ptr, type *t
// Value is offset
// return ptr + sizeof(t) * Value
// return type *t
class IRGetPtrInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* src_;
    IRValue* index_;
    ~IRGetPtrInst() override = default;

private:
    IRGetPtrInst() = default;
    explicit IRGetPtrInst(IRValue* src, IRValue* index, const IRType* ty,
                          const std::string& name = {}) :
        IRInstruction(IRValueKind::IR_GET_PTR, ty, name),
        src_(src),
        index_(index)
    {
    }
};

// GetElementPointer ::= "getelemptr" SYMBOL "," Value;
// SYMBOL is pointer of array ptr, type *[t, len]
// Value is offset
// return ptr + sizeof(t) * Value
// return type *t
class IRGetElemPtrInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* src_;
    IRValue* index_;
    ~IRGetElemPtrInst() override = default;

private:
    IRGetElemPtrInst() = default;
    explicit IRGetElemPtrInst(IRValue* src, IRValue* index, const IRType* ty,
                              const std::string& name = {}) :
        IRInstruction(IRValueKind::IR_GET_ELEM_PTR, ty, name),
        src_(src),
        index_(index)
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
    IRValue* init_;
    ~IRGlobalAllocInst() override = default;

private:
    IRGlobalAllocInst() = default;
    explicit IRGlobalAllocInst(IRValue* init, const IRType* ty, const std::string& name = {}) :
        IRInstruction(IRValueKind::IR_GLOBALALLOC, ty, name),
        init_(init)
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
    IRValue* dst_; // return value（nullptr represent void）
    ~IRReturnInst() override = default;

private:
    IRReturnInst() = default;
    explicit IRReturnInst(IRValue* dst, const std::string& name = {}) : IRInstruction(IRValueKind::IR_RETURN, nullptr, name), dst_(dst)
    {
    }
};

// example: %2 = add %0, %1
// name represent inst result
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

class IRBranchInst : public IRInstruction
{
    friend class IRModule;

public:
    IRValue* cond_; // binary inst / constant value / load inst
    IRBasicBlock* if_basic_block_;
    IRBasicBlock* else_basic_block_; // else bb or end merge bb

    ~IRBranchInst() override = default;

private:
    IRBranchInst() = default;

    explicit IRBranchInst(IRValue* cond, IRBasicBlock* if_block, IRBasicBlock* else_block, const IRType* ty, const std::string& name = {}) :
        IRInstruction(IRValueKind::IR_BRANCH, ty, name),
        cond_(cond),
        if_basic_block_(if_block),
        else_basic_block_(else_block)
    {
    }
};

class IRJumpInst : public IRInstruction
{
    friend class IRModule;

public:
    IRBasicBlock* jump_basic_block_;

private:
    IRJumpInst() = default;

    explicit IRJumpInst(IRBasicBlock* jump_block, const IRType* ty,
                        const std::string& name = {}) :
        IRInstruction(IRValueKind::IR_JUMP, ty, name),
        jump_basic_block_(jump_block)
    {
    }
};

/*
 * Block ::= SYMBOL ":" {Statement} EndStatement;
 * Statement ::= SymbolDef | Store | FunCall;
 * EndStatement ::= Branch | Jump | Return;

 * br, jump or ret can be the basic block's termination instruction
 */
class IRBasicBlock
{
    friend class IRModule;

public:
    std::string name_;
    std::vector<IRValue*> insts_;

private:
    IRBasicBlock() = default;
    explicit IRBasicBlock(const std::string& name, std::vector<IRValue*> insts = {}) :
        name_(name),
        insts_(insts)
    {
    }
};

/*
 * FunDef ::= "fun" SYMBOL "(" [FunParams] ")" [":" Type] "{" FunBody "}";
 * FunParams ::= SYMBOL ":" Type {"," SYMBOL ":" Type};
 * FunBody ::= {Block};

 * FunDef used to define a function
 * FunParams used to declare args name and type
 * FunBody consists one or more basic block
 * first basic block called entry basic block
 * and entry block can't have any predecessor
 */

class IRFunction
{
    friend class IRModule;

public:
    const IRFunctionType* type_;
    std::string name_;
    std::vector<IRBasicBlock*> basic_blocks_;
    std::vector<IRValue*> params_;
    bool is_declaration_ = false;

private:
    IRFunction() = default;
    explicit IRFunction(const IRFunctionType* type, const std::string& name, bool is_declaration = false) :
        type_(type),
        name_(name),
        basic_blocks_({}),
        params_({}),
        is_declaration_(is_declaration)
    {
    }
};

// IRModule
// * const value store in RewindIRBuilder constant_cache_
class IRModule
{
public:
    IRModule() = default;
    IRModule(const IRModule&) = delete;
    IRModule& operator=(const IRModule&) = delete;
    IRModule(IRModule&&) noexcept = default;
    IRModule& operator=(IRModule&&) noexcept = default;

    IRFunction* make_function(const IRFunctionType* type, const std::string& name,
                              bool is_declaration = false);

    IRBasicBlock* make_basic_block(const std::string& name, std::vector<IRValue*> insts = {});

    template <typename T, typename... Args>
    T* make_value(Args&&... args);

    // void append_global_value(IRValue& value);

    void append_param(IRFunction& function, IRValue& value);

    void append_global_value(IRValue& value);

    void append_basic_block(IRFunction& function, IRBasicBlock& block);

    void append_value(IRBasicBlock& block, IRValue& value);

    std::vector<IRFunction*> funcs_;
    std::vector<IRValue*> global_values_;

private:
    std::vector<std::unique_ptr<IRFunction>> func_storage_;
    std::vector<std::unique_ptr<IRBasicBlock>> bb_storage_;
    std::vector<std::unique_ptr<IRValue>> value_storage_;
};

inline IRFunction* IRModule::make_function(const IRFunctionType* type, const std::string& name,
                                           bool is_declaration)
{
    std::unique_ptr<IRFunction> func(new IRFunction(type, name, is_declaration));
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

inline void IRModule::append_param(IRFunction& function, IRValue& value)
{
    function.params_.push_back(&value);
}

inline void IRModule::append_global_value(IRValue& value)
{
    global_values_.push_back(&value);
}

// function add basic block
inline void IRModule::append_basic_block(IRFunction& function, IRBasicBlock& block)
{
    function.basic_blocks_.push_back(&block);
}

// basic block add value
inline void IRModule::append_value(IRBasicBlock& block, IRValue& value)
{
    block.insts_.push_back(&value);
}

} // namespace rewind_ir
