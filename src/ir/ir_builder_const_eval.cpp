#include "ir_builder.h"
#include "ast.h"
#include "symbol_table.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace rewind_ir
{
namespace
{

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
        throw std::runtime_error(std::string("AST type mismatch, expected: ") + expected_name);
    }
    return *p;
}

inline int32_t eval_binary_op(BinaryOp op, int32_t a, int32_t b)
{
    switch (op) {
    case BinaryOp::MUL:
        return a * b;
    case BinaryOp::DIV:
        return a / b;
    case BinaryOp::MOD:
        return a % b;
    case BinaryOp::ADD:
        return a + b;
    case BinaryOp::SUB:
        return a - b;
    case BinaryOp::LT:
        return a < b;
    case BinaryOp::GT:
        return a > b;
    case BinaryOp::LE:
        return a <= b;
    case BinaryOp::GE:
        return a >= b;
    case BinaryOp::EQ:
        return a == b;
    case BinaryOp::NEQ:
        return a != b;
    default:
        break;
    }
    throw std::runtime_error("unsupported BinaryOp for const eval");
}

class ConstEvaluator
{
public:
    using LookupValue =
        std::function<std::optional<SymbolTable::LookupResult>(const std::string&)>;

    explicit ConstEvaluator(LookupValue lookup_value) : lookup_value_(std::move(lookup_value))
    {
    }

    int32_t eval_exp(const ExpAST& ast)
    {
        const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
        return eval_lor_exp(lor_exp);
    }

private:
    int32_t eval_lor_exp(const LOrExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const LOrExpAST::Simple& simple) -> int32_t {
                    const auto& land_exp =
                        expect_node<LAndExpAST>(*simple.land_exp, "LAndExpAST");
                    return eval_land_exp(land_exp);
                },
                [&](const LOrExpAST::Binary& binary) -> int32_t {
                    const auto& lor_exp =
                        expect_node<LOrExpAST>(*binary.lor_exp, "LOrExpAST");
                    const auto& land_exp =
                        expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                    auto lhs = eval_lor_exp(lor_exp);
                    auto rhs = eval_land_exp(land_exp);
                    return lhs || rhs;
                }},
            ast.payload);
    }

    int32_t eval_land_exp(const LAndExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const LAndExpAST::Simple& simple) -> int32_t {
                    const auto& eq_exp =
                        expect_node<EqExpAST>(*simple.eq_exp, "EqExpAST");
                    return eval_eq_exp(eq_exp);
                },
                [&](const LAndExpAST::Binary& binary) -> int32_t {
                    const auto& land_exp =
                        expect_node<LAndExpAST>(*binary.land_exp, "LAndExpAST");
                    const auto& eq_exp =
                        expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                    auto lhs = eval_land_exp(land_exp);
                    auto rhs = eval_eq_exp(eq_exp);
                    return lhs && rhs;
                }},
            ast.payload);
    }

    int32_t eval_eq_exp(const EqExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const EqExpAST::Simple& simple) -> int32_t {
                    const auto& rel_exp =
                        expect_node<RelExpAST>(*simple.rel_exp, "RelExpAST");
                    return eval_rel_exp(rel_exp);
                },
                [&](const EqExpAST::Binary& binary) -> int32_t {
                    const auto& eq_exp =
                        expect_node<EqExpAST>(*binary.eq_exp, "EqExpAST");
                    const auto& rel_exp =
                        expect_node<RelExpAST>(*binary.rel_exp, "RelExpAST");
                    auto lhs = eval_eq_exp(eq_exp);
                    auto rhs = eval_rel_exp(rel_exp);
                    return eval_binary_op(binary.op, lhs, rhs);
                }},
            ast.payload);
    }

    int32_t eval_rel_exp(const RelExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const RelExpAST::Simple& s) -> int32_t {
                    const auto& add_exp =
                        expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                    return eval_add_exp(add_exp);
                },
                [&](const RelExpAST::Binary& b) -> int32_t {
                    const auto& rel_exp =
                        expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                    const auto& add_exp =
                        expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                    auto lhs = eval_rel_exp(rel_exp);
                    auto rhs = eval_add_exp(add_exp);
                    return eval_binary_op(b.op, lhs, rhs);
                }},
            ast.payload);
    }

    int32_t eval_add_exp(const AddExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const AddExpAST::Simple& s) -> int32_t {
                    const auto& mul_exp =
                        expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                    return eval_mul_exp(mul_exp);
                },
                [&](const AddExpAST::Binary& b) -> int32_t {
                    const auto& add_exp =
                        expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                    const auto& mul_exp =
                        expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                    auto lhs = eval_add_exp(add_exp);
                    auto rhs = eval_mul_exp(mul_exp);
                    return eval_binary_op(b.op, lhs, rhs);
                }},
            ast.payload);
    }

    int32_t eval_mul_exp(const MulExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const MulExpAST::Simple& s) -> int32_t {
                    const auto& unary_exp =
                        expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                    return eval_unary_exp(unary_exp);
                },
                [&](const MulExpAST::Binary& b) -> int32_t {
                    const auto& mul_exp =
                        expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                    const auto& unary_exp =
                        expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");
                    auto lhs = eval_mul_exp(mul_exp);
                    auto rhs = eval_unary_exp(unary_exp);
                    return eval_binary_op(b.op, lhs, rhs);
                }},
            ast.payload);
    }

    int32_t eval_unary_exp(const UnaryExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const UnaryExpAST::Primary& p) -> int32_t {
                    const auto& primary =
                        expect_node<PrimaryExpAST>(*p.exp, "PrimaryExpAST");
                    return eval_primary_exp(primary);
                },
                [&](const UnaryExpAST::Unary& u) -> int32_t {
                    const auto& unary_exp =
                        expect_node<UnaryExpAST>(*u.exp, "UnaryExpAST");
                    auto operand = eval_unary_exp(unary_exp);
                    switch (u.op) {
                    case UnaryOp::PLUS:
                        return +operand;
                    case UnaryOp::MINUS:
                        return -operand;
                    case UnaryOp::NOT:
                        return !operand;
                    }
                    throw std::runtime_error("invalid UnaryOp");
                },
                [&](const UnaryExpAST::FuncCall&) -> int32_t {
                    throw std::runtime_error("can't handle function call");
                }},
            ast.payload);
    }

    int32_t eval_primary_exp(const PrimaryExpAST& ast)
    {
        return std::visit(
            overloaded{
                [&](const PrimaryExpAST::Number& n) -> int32_t { return n.value; },
                [&](const PrimaryExpAST::Expression& e) -> int32_t {
                    const auto& exp = expect_node<ExpAST>(*e.exp, "ExpAST");
                    return eval_exp(exp);
                },
                [&](const PrimaryExpAST::LValue& lvalue) -> int32_t {
                    const auto& lval = expect_node<LValAST>(*lvalue.lval, "LValAST");
                    auto value = lookup_value_(lval.ident);

                    if (!value) {
                        throw std::runtime_error("undefined variable: " + lval.ident);
                    }

                    if (std::holds_alternative<SymbolTable::Var>(*value)) {
                        throw std::runtime_error(lval.ident + " is not constexpr");
                    }

                    return std::get<SymbolTable::Constant>(*value).value;
                }},
            ast.payload);
    }

    LookupValue lookup_value_;
};

} // namespace

int32_t RewindIRBuilder::eval_exp(const ExpAST& ast)
{
    ConstEvaluator evaluator{
        [this](const std::string& name) {
            return lookup_value(name);
        }};
    return evaluator.eval_exp(ast);
}

} // namespace rewind_ir
