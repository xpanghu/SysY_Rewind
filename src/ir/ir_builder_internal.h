#pragma once

#include "ast.h"
#include "ir_type.h"
#include "rewind_ir.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rewind_ir::builder_detail
{

inline const IRInt32Type* get_i32_type()
{
    return IRTypeContext::instance().getInt32();
}

inline const IRUnitType* get_unit_type()
{
    return IRTypeContext::instance().getUnit();
}

inline const IRPointerType* get_pointer_type(const IRType* base_type)
{
    return IRTypeContext::instance().getPointer(base_type);
}

inline const IRArrayType* get_array_type(const std::vector<int32_t>& dims,
                                         const IRType* elem_type)
{
    if (dims.empty()) {
        return elem_type->is_array() ? elem_type->as<IRArrayType>() : nullptr;
    }

    const IRType* current_type = elem_type;
    for (int i = static_cast<int>(dims.size()) - 1; i >= 0; --i) {
        current_type = IRTypeContext::instance().getArray(
            current_type,
            static_cast<size_t>(dims[i]));
    }

    return current_type->as<IRArrayType>();
}

inline const IRFunctionType* get_function_type(std::vector<const IRType*> params,
                                               const IRType* ret)
{
    return IRTypeContext::instance().getFunction(std::move(params), ret);
}

inline const IRType* get_array_storage_type(const IRValue* storage)
{
    if (!storage->type_->is_pointer()) {
        return nullptr;
    }

    const auto* pointer_type = storage->type_->as<IRPointerType>();
    if (pointer_type->base_type->is_array() || pointer_type->base_type->is_pointer()) {
        return pointer_type->base_type;
    }

    return nullptr;
}

inline bool is_array_storage(const IRValue* storage)
{
    return get_array_storage_type(storage) != nullptr;
}

inline bool is_call_arg_type_compatible(const IRType* actual, const IRType* expected)
{
    if (actual == expected) {
        return true;
    }

    if (!actual->is_pointer() || !expected->is_pointer()) {
        return false;
    }

    const auto* actual_base = actual->as<IRPointerType>()->base_type;
    if (!actual_base->is_array()) {
        return false;
    }

    const auto* decayed_type =
        get_pointer_type(actual_base->as<IRArrayType>()->element_type);
    return decayed_type == expected;
}

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template <typename T>
const T& expect_node(const BaseAST& node, const char* expected_name)
{
    const auto* p = dynamic_cast<const T*>(&node);
    if (p == nullptr) {
        throw std::runtime_error(std::string("AST type mismatch, expected: ")
                                 + expected_name);
    }
    return *p;
}

inline IRBinaryOp ast_op_to_ir_op(BinaryOp op)
{
    switch (op) {
    case BinaryOp::MUL:
        return IRBinaryOp::MUL;
    case BinaryOp::DIV:
        return IRBinaryOp::DIV;
    case BinaryOp::MOD:
        return IRBinaryOp::MOD;
    case BinaryOp::ADD:
        return IRBinaryOp::ADD;
    case BinaryOp::SUB:
        return IRBinaryOp::SUB;
    case BinaryOp::LAND:
        return IRBinaryOp::AND;
    case BinaryOp::LOR:
        return IRBinaryOp::OR;
    case BinaryOp::EQ:
        return IRBinaryOp::EQ;
    case BinaryOp::NEQ:
        return IRBinaryOp::NEQ;
    case BinaryOp::LT:
        return IRBinaryOp::LT;
    case BinaryOp::GT:
        return IRBinaryOp::GT;
    case BinaryOp::LE:
        return IRBinaryOp::LE;
    case BinaryOp::GE:
        return IRBinaryOp::GE;
    }
    throw std::runtime_error("unsupported BinaryOp kind");
}

inline const IRType* func_return_type(const FuncType type)
{
    if (type == FuncType::VOID) {
        return get_unit_type();
    }
    if (type == FuncType::INT) {
        return get_i32_type();
    }
    throw std::runtime_error("func type only support void / int");
}

inline const LValAST* try_extract_lvalue(const BaseAST& node)
{
    if (const auto* lval = dynamic_cast<const LValAST*>(&node)) {
        return lval;
    }

    if (const auto* exp = dynamic_cast<const ExpAST*>(&node)) {
        if (!exp->lor_exp) {
            return nullptr;
        }
        return try_extract_lvalue(*exp->lor_exp);
    }

    if (const auto* lor = dynamic_cast<const LOrExpAST*>(&node)) {
        if (const auto* simple = std::get_if<LOrExpAST::Simple>(&lor->payload)) {
            return try_extract_lvalue(*simple->land_exp);
        }
        return nullptr;
    }

    if (const auto* land = dynamic_cast<const LAndExpAST*>(&node)) {
        if (const auto* simple = std::get_if<LAndExpAST::Simple>(&land->payload)) {
            return try_extract_lvalue(*simple->eq_exp);
        }
        return nullptr;
    }

    if (const auto* eq = dynamic_cast<const EqExpAST*>(&node)) {
        if (const auto* simple = std::get_if<EqExpAST::Simple>(&eq->payload)) {
            return try_extract_lvalue(*simple->rel_exp);
        }
        return nullptr;
    }

    if (const auto* rel = dynamic_cast<const RelExpAST*>(&node)) {
        if (const auto* simple = std::get_if<RelExpAST::Simple>(&rel->payload)) {
            return try_extract_lvalue(*simple->add_exp);
        }
        return nullptr;
    }

    if (const auto* add = dynamic_cast<const AddExpAST*>(&node)) {
        if (const auto* simple = std::get_if<AddExpAST::Simple>(&add->payload)) {
            return try_extract_lvalue(*simple->mul_exp);
        }
        return nullptr;
    }

    if (const auto* mul = dynamic_cast<const MulExpAST*>(&node)) {
        if (const auto* simple = std::get_if<MulExpAST::Simple>(&mul->payload)) {
            return try_extract_lvalue(*simple->unary_exp);
        }
        return nullptr;
    }

    if (const auto* unary = dynamic_cast<const UnaryExpAST*>(&node)) {
        if (const auto* primary = std::get_if<UnaryExpAST::Primary>(&unary->payload)) {
            return try_extract_lvalue(*primary->exp);
        }
        return nullptr;
    }

    if (const auto* primary = dynamic_cast<const PrimaryExpAST*>(&node)) {
        if (const auto* lvalue = std::get_if<PrimaryExpAST::LValue>(&primary->payload)) {
            return dynamic_cast<const LValAST*>(lvalue->lval.get());
        }
        if (const auto* expr = std::get_if<PrimaryExpAST::Expression>(&primary->payload)) {
            return try_extract_lvalue(*expr->exp);
        }
    }

    return nullptr;
}

} // namespace rewind_ir::builder_detail
