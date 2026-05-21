#include "semantic_checks.h"
#include "rewind_ir.h"
#include <stdexcept>

namespace rewind_ir::semantic
{
namespace
{

bool is_call_argument_type_compatible(const IRType* actual, const IRType* expected)
{
    if (actual == expected) {
        return true;
    }

    if (actual == nullptr || expected == nullptr) {
        return false;
    }

    if (!actual->is_pointer() || !expected->is_pointer()) {
        return false;
    }

    const auto* actual_base = actual->as<IRPointerType>()->base_type;
    if (!actual_base->is_array()) {
        return false;
    }

    const auto* decayed_type =
        IRTypeContext::instance().getPointer(actual_base->as<IRArrayType>()->element_type);
    return decayed_type == expected;
}

bool is_array_storage(const IRValue* storage)
{
    if (storage == nullptr || storage->type_ == nullptr || !storage->type_->is_pointer()) {
        return false;
    }

    const auto* base = storage->type_->as<IRPointerType>()->base_type;
    return base != nullptr && (base->is_array() || base->is_pointer());
}

} // namespace

VariableSemanticInfo require_variable_symbol(
    const std::optional<SymbolTable::LookupResult>& lookup,
    const std::string& name)
{
    if (!lookup) {
        throw std::runtime_error(name + " is not exist");
    }

    if (std::holds_alternative<SymbolTable::Constant>(*lookup)) {
        throw std::runtime_error(name + " is constant");
    }

    const auto var = std::get<SymbolTable::Var>(*lookup);
    return VariableSemanticInfo{var.alloc, var.is_const};
}

VariableSemanticInfo require_mutable_variable_symbol(
    const std::optional<SymbolTable::LookupResult>& lookup,
    const std::string& name)
{
    auto var = require_variable_symbol(lookup, name);
    if (var.is_const) {
        throw std::runtime_error(name + " is const");
    }
    return var;
}

VariableSemanticInfo require_array_argument_storage(
    const std::optional<SymbolTable::LookupResult>& lookup)
{
    if (!lookup || !std::holds_alternative<SymbolTable::Var>(*lookup)) {
        throw_call_argument_type_mismatch();
    }

    const auto var = std::get<SymbolTable::Var>(*lookup);
    if (!is_array_storage(var.alloc)) {
        throw_call_argument_type_mismatch();
    }

    return VariableSemanticInfo{var.alloc, var.is_const};
}

void require_function_defined(const IRFunction* callee, const std::string& name)
{
    if (callee == nullptr) {
        throw std::runtime_error("undefined function: " + name);
    }
}

void require_call_argument_count(const IRFunction& callee,
                                 size_t actual_count,
                                 const std::string& name)
{
    if (actual_count != callee.type_->params.size()) {
        throw std::runtime_error("function argument count mismatch: " + name);
    }
}

void require_call_argument_type(const IRType* actual,
                                const IRType* expected,
                                const std::string& name)
{
    if (!is_call_argument_type_compatible(actual, expected)) {
        throw std::runtime_error("function argument type mismatch: " + name);
    }
}

void require_array_index_count(const std::string& name,
                               size_t array_dim,
                               size_t provided_indices,
                               bool allow_array_decay)
{
    if (array_dim < provided_indices) {
        throw std::runtime_error("too many indices for array: " + name);
    }

    if (array_dim > provided_indices && !allow_array_decay) {
        if (provided_indices == 0) {
            throw std::runtime_error("array is not assignable without index: " + name);
        }
        throw std::runtime_error("less indices for array: " + name);
    }
}

void require_pointer_array_index_count(const std::string& name,
                                       size_t remaining_dim,
                                       size_t provided_remaining_indices,
                                       bool allow_array_decay)
{
    if (provided_remaining_indices > remaining_dim) {
        throw std::runtime_error("too many indices for array: " + name);
    }

    if (provided_remaining_indices < remaining_dim && !allow_array_decay) {
        throw std::runtime_error("less indices for array: " + name);
    }
}

void require_array_decay_allowed(const std::string& name, bool allow_array_decay)
{
    if (!allow_array_decay) {
        throw std::runtime_error("array is not assignable without index: " + name);
    }
}

void require_scalar_without_indices(const std::string& name, bool has_indices)
{
    if (has_indices) {
        throw std::runtime_error(name + " is not array");
    }
}

void require_return_value_compatible(const IRType* return_type, bool has_return_value)
{
    if (has_return_value && return_type != nullptr && return_type->is_unit()) {
        throw std::runtime_error("void function should not return a value");
    }
}

void require_loop_control_inside_loop(bool in_loop, const char* control_name)
{
    if (!in_loop) {
        throw std::runtime_error(std::string(control_name) + " used outside while");
    }
}

[[noreturn]] void throw_call_argument_type_mismatch()
{
    throw std::runtime_error("actual param type not match formal param");
}

} // namespace rewind_ir::semantic
