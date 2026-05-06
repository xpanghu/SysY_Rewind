#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>

namespace rewind_ir
{

class IRValue;
class IRFunction;

class SymbolTable
{
public:
    struct Constant {
        int32_t value;
    };

    struct Var {
        IRValue* alloc;
        bool is_const = false;
    };

    using LookupResult = std::variant<Constant, Var>;

private:
    // Scope stack + mapping of constants or variables
    std::vector<std::unordered_map<std::string, LookupResult>> scopes_;

    // Module-global function table
    std::unordered_map<std::string, IRFunction*> func_table_;

public:
    SymbolTable() = default;

    // 进入新作用域
    void enter_scope();

    // 退出当前作用域
    void exit_scope();

    void define_const(const std::string& name, int32_t value);

    void define_var(const std::string& name, IRValue* alloc, bool is_const = false);

    std::optional<LookupResult> lookup_value(const std::string& name) const;

    void define_function(const std::string& name, IRFunction* func);

    IRFunction* lookup_function(const std::string& name) const;
};

} // namespace rewind_ir
