#include "symbol_table.h"
#include "rewind_ir.h"
#include <cstdint>
#include <stdexcept>

namespace rewind_ir
{

void SymbolTable::enter_scope()
{
    scopes_.emplace_back();
}

void SymbolTable::exit_scope()
{
    if (scopes_.empty()) {
        throw std::runtime_error("exit_scope: no scope to exit");
    }
    scopes_.pop_back();
}

// define const
void SymbolTable::define_const(const std::string& name, int32_t value)
{
    auto& scope = scopes_.back();
    if (scope.count(name)) {
        throw std::runtime_error("redefinition of const: " + name);
    }
    scope[name] = SymbolTable::Constant{value};
}

// define variable
void SymbolTable::define_var(const std::string& name, IRValue* alloc, bool is_const)
{
    if (alloc == nullptr) {
        throw std::runtime_error("define_var: null alloc");
    }
    auto& scope = scopes_.back();
    if (scope.count(name)) {
        throw std::runtime_error("redefinition of variable: " + name);
    }
    scope[name] = SymbolTable::Var{alloc, is_const};
}

std::optional<SymbolTable::LookupResult> SymbolTable::lookup_value(const std::string& name) const
{
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        if (auto found = it->find(name); found != it->end()) {
            return found->second;
        }
    }

    return std::nullopt;
}

void SymbolTable::define_function(const std::string& name, IRFunction* func)
{
    if (func == nullptr) {
        throw std::runtime_error("define_function: null function");
    }
    if (func_table_.count(name)) {
        throw std::runtime_error("redefinition of function: " + name);
    }
    func_table_[name] = func;
}

IRFunction* SymbolTable::lookup_function(const std::string& name) const
{
    auto it = func_table_.find(name);
    if (it == func_table_.end()) {
        return nullptr;
    }
    return it->second;
}
} // namespace rewind_ir
