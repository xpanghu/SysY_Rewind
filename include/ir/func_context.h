#pragma once

#include "rewind_ir.h"
#include "symbol_table.h"
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace rewind_ir
{

// Lightweight per-function lowering state.
//
// Responsibility split:
// - RewindIRBuilder keeps module-scope state, such as global symbols
// - IRModule remains the owner/factory for IR objects
// - FuncContext only tracks the mutable state needed while lowering
//   one function body
//
// The current builder can adopt this context incrementally.
class FuncContext
{
public:
    /*
     * break and continue relevant content
     * LoopTargets store break address and continue address
     * loop_stack_ record current LoopTargets
     */
    struct LoopTargets {
        IRBasicBlock* break_target;
        IRBasicBlock* continue_target;
    };

    void push_loop(IRBasicBlock& break_target, IRBasicBlock& continue_target)
    {
        loop_stack_.push_back({&break_target, &continue_target});
    }

    void pop_loop()
    {
        if (loop_stack_.empty()) {
            throw std::runtime_error("loop stack is empty");
        }
        loop_stack_.pop_back();
    }

    bool in_loop() const
    {
        return !loop_stack_.empty();
    }

    LoopTargets current_loop() const
    {
        if (loop_stack_.empty()) {
            throw std::runtime_error("current loop is not set");
        }
        return loop_stack_.back();
    }

    /*
     * ScopeGuard
     */
    class ScopeGuard
    {
    public:
        explicit ScopeGuard(FuncContext& ctx) : ctx_(&ctx)
        {
            ctx_->push_scope();
        }

        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

        ScopeGuard(ScopeGuard&& other) noexcept : ctx_(other.ctx_), active_(other.active_)
        {
            other.ctx_ = nullptr;
            other.active_ = false;
        }

        ScopeGuard& operator=(ScopeGuard&&) = delete;

        ~ScopeGuard()
        {
            if (active_ && ctx_ != nullptr) {
                ctx_->pop_scope();
            }
        }

        void dismiss() noexcept
        {
            active_ = false;
        }

    private:
        FuncContext* ctx_ = nullptr;
        bool active_ = true;
    };

    IRModule& module() const
    {
        return module_;
    }

    IRFunction& current_function() const
    {
        return *current_function_;
    }

    IRBasicBlock* current_block_or_null() const
    {
        return current_block_;
    }

    bool has_current_block() const
    {
        return current_block_ != nullptr;
    }

    IRBasicBlock& current_block()
    {
        if (current_block_ == nullptr) {
            throw std::runtime_error("current block is not set");
        }
        return *current_block_;
    }

    const IRBasicBlock& current_block() const
    {
        if (current_block_ == nullptr) {
            throw std::runtime_error("current block is not set");
        }
        return *current_block_;
    }

    void set_current_block(IRBasicBlock& block)
    {
        current_block_ = &block;
    }

    void clear_current_block()
    {
        current_block_ = nullptr;
    }

    ScopeGuard make_scope()
    {
        return ScopeGuard(*this);
    }

    void push_scope()
    {
        symbols_.enter_scope();
    }

    void pop_scope()
    {
        symbols_.exit_scope();
    }

    SymbolTable& symbols()
    {
        return symbols_;
    }

    const SymbolTable& symbols() const
    {
        return symbols_;
    }

    // %number : SSA values
    std::string next_percent_name()
    {
        return "%" + std::to_string(value_counter_++);
    }

    // @string : alloc/object-like names
    std::string next_at_name(const std::string& ident)
    {
        int& counter = alloc_name_counter_[ident];
        return "@" + ident + "_" + std::to_string(++counter);
    }

    // %string : basic blocks
    std::string next_block_name(const std::string& hint)
    {
        int& counter = block_name_counter_[hint];
        return "%" + hint + "_" + std::to_string(++counter);
    }

    IRBasicBlock& create_function_block(const std::string& hint)
    {
        auto* block = module_.make_basic_block(next_block_name(hint));
        module_.append_basic_block(current_function(), *block);
        return *block;
    }

    template <typename T, typename... Args>
    T& create_block_value(Args&&... args)
    {
        static_assert(std::is_base_of_v<IRInstruction, T>,
                      "create_block_value<T>: T must derive from IRInstruction");
        auto* value = module_.make_value<T>(std::forward<Args>(args)...);
        module_.append_value(current_block(), *value);
        return *value;
    }

    IRReturnInst& terminate_with_return(IRValue* value)
    {
        auto& inst = create_block_value<IRReturnInst>(value);
        clear_current_block();
        return inst;
    }

    IRBranchInst& terminate_with_branch(IRValue* cond,
                                        IRBasicBlock& if_block,
                                        IRBasicBlock& else_block)
    {
        auto& inst = create_block_value<IRBranchInst>(
            cond,
            &if_block,
            &else_block,
            IRTypeContext::instance().getUnit());
        clear_current_block();
        return inst;
    }

    IRJumpInst& terminate_with_jump(IRBasicBlock& target)
    {
        auto& inst = create_block_value<IRJumpInst>(
            &target,
            IRTypeContext::instance().getUnit());
        clear_current_block();
        return inst;
    }

    FuncContext(IRModule& module, IRFunction& function) :
        module_(module),
        current_function_(&function)
    {
    }

private:
    IRModule& module_;
    IRFunction* current_function_ = nullptr;
    IRBasicBlock* current_block_ = nullptr;

    SymbolTable symbols_; // function symbol table
    std::vector<LoopTargets> loop_stack_;
    std::unordered_map<std::string, int> alloc_name_counter_;
    std::unordered_map<std::string, int> block_name_counter_;
    int value_counter_ = 0;
};

} // namespace rewind_ir
