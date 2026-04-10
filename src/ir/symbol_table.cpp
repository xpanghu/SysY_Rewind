#include "symbol_table.h"
#include "rewind_ir.h"
#include <cstdint>
#include <stdexcept>
#include <variant>

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

void SymbolTable::define_const(const std::string& name, int32_t value)
{
    auto& scope = scopes_.back();
    if (scope.count(name)) {
        throw std::runtime_error("redefinition of const: " + name);
    }
    scope[name] = SymbolTable::Const{value};
}

// define variable
void SymbolTable::define_var(const std::string& name, IRValue* alloc)
{
    auto& scope = scopes_.back();
    if (scope.count(name)) {
        throw std::runtime_error("redefinition of vaiable: " + name);
    }
    scope[name] = SymbolTable::Var{alloc};
}

// Search by variable name
// 1. return int32_t represent const
// 2. return IRValue* represent variable (alloc instruction)
// 3. return nullopt represent not exist
std::optional<std::variant<int32_t, IRValue*>> SymbolTable::lookup(const std::string& name) const
{
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        if (auto found = it->find(name); found != it->end()) {
            auto value = found->second;
            // 查找变量中，查找到常量，语义错误
            if (std::holds_alternative<SymbolTable::Const>(value)) {
                return std::get<SymbolTable::Const>(value).value;
            } else {
                return std::get<SymbolTable::Var>(value).alloc;
            }
        }
    }

    return std::nullopt;
}
} // namespace rewind_ir
