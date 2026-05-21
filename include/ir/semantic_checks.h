#pragma once

#include "ir_type.h"
#include "symbol_table.h"
#include <cstddef>
#include <optional>
#include <string>

namespace rewind_ir
{

class IRFunction;
class IRValue;

namespace semantic
{

struct VariableSemanticInfo {
    IRValue* storage = nullptr;
    bool is_const = false;
};

VariableSemanticInfo require_variable_symbol(
    const std::optional<SymbolTable::LookupResult>& lookup,
    const std::string& name);

VariableSemanticInfo require_mutable_variable_symbol(
    const std::optional<SymbolTable::LookupResult>& lookup,
    const std::string& name);

VariableSemanticInfo require_array_argument_storage(
    const std::optional<SymbolTable::LookupResult>& lookup);

void require_function_defined(const IRFunction* callee, const std::string& name);
void require_call_argument_count(const IRFunction& callee,
                                 size_t actual_count,
                                 const std::string& name);
void require_call_argument_type(const IRType* actual,
                                const IRType* expected,
                                const std::string& name);

void require_array_index_count(const std::string& name,
                               size_t array_dim,
                               size_t provided_indices,
                               bool allow_array_decay);
void require_pointer_array_index_count(const std::string& name,
                                       size_t remaining_dim,
                                       size_t provided_remaining_indices,
                                       bool allow_array_decay);
void require_array_decay_allowed(const std::string& name, bool allow_array_decay);
void require_scalar_without_indices(const std::string& name, bool has_indices);

void require_return_value_compatible(const IRType* return_type, bool has_return_value);
void require_loop_control_inside_loop(bool in_loop, const char* control_name);

[[noreturn]] void throw_call_argument_type_mismatch();

} // namespace semantic

} // namespace rewind_ir
