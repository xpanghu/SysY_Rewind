#pragma once

#include "ast.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rewind_ir {

class SymbolTable {
public:
    SymbolTable() = default;

    // 进入新作用域
    void enter_scope();

    // 退出当前作用域
    void exit_scope();

    // 定义常量
    void define_const(const std::string& name, int32_t value);

    // 查询常量
    std::optional<int32_t> lookup_const(const std::string& name) const;

private:
    // 作用域栈 + 常量映射
    std::vector<std::unordered_map<std::string, int32_t>> const_scopes_;
};

} // namespace rewind_ir
