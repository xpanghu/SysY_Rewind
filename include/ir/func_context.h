#pragma once

#include "rewind_ir.h"
#include "symbol_table.h"
#include <string>
#include <unordered_map>

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

    FuncContext(IRModule& module, IRFunction& function) : module_(module), current_function_(&function)
    {
    }

    IRModule& module() const
    {
        return module_;
    }

    IRFunction& current_function() const
    {
        return *current_function_;
    }

    IRBasicBlock* current_block() const
    {
        return current_block_;
    }

    bool has_current_block() const
    {
        return current_block_ != nullptr;
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

    std::string next_value_name()
    {
        return "%" + std::to_string(value_counter_++);
    }

    std::string next_alloc_name(const std::string& ident)
    {
        int& counter = alloc_name_counter_[ident];
        ++counter;
        return "@" + ident + "_" + std::to_string(counter);
    }

    std::string next_block_name(const std::string& hint)
    {
        return "%" + hint + "_" + std::to_string(block_counter_++);
    }

private:
    IRModule& module_;
    IRFunction* current_function_ = nullptr;
    IRBasicBlock* current_block_ = nullptr;

    SymbolTable symbols_;
    std::unordered_map<std::string, int> alloc_name_counter_;
    int value_counter_ = 0;
    int block_counter_ = 0;
};

} // namespace rewind_ir
