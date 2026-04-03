#include "ir/symbol_table.h"

namespace rewind_ir {

void SymbolTable::enter_scope()
{
    const_scopes_.emplace_back();
}

void SymbolTable::exit_scope()
{
    if (const_scopes_.empty()) {
        throw std::runtime_error("exit_scope: no scope to exit");
    }
    const_scopes_.pop_back();
}

void SymbolTable::define_const(const std::string& name, int32_t value)
{
    auto& scope = const_scopes_.back();
    if (scope.count(name)) {
        throw std::runtime_error("redefinition of const: " + name);
    }
    scope[name] = value;
}

std::optional<int32_t> SymbolTable::lookup_const(const std::string& name) const
{
    for (auto it = const_scopes_.rbegin(); it != const_scopes_.rend(); ++it) {
        if (auto found = it->find(name); found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

} // namespace rewind_ir
