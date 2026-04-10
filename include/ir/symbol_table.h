#pragma once

#include "ast.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>

namespace rewind_ir
{

class IRValue;

class SymbolTable
{
private:
    struct Const {
        int32_t value;
    };

    struct Var {
        IRValue* alloc;
    };

    using Payload = std::variant<Const, Var>;

    // Scope stack + mapping of constants or variables
    std::vector<std::unordered_map<std::string, Payload>> scopes_;

public:
    SymbolTable() = default;

    // 进入新作用域
    void enter_scope();

    // 退出当前作用域
    void exit_scope();

    void define_const(const std::string& name, int32_t value);

    std::optional<std::variant<int32_t, IRValue*>> lookup(const std::string& name) const;

    void define_var(const std::string& name, IRValue* alloc);
};

} // namespace rewind_ir
