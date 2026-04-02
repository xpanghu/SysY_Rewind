#include "rewind_ir_builder.h"
#include "ast.h"

namespace rewind_ir {
namespace {

    template <typename T>
    const T& expect_node(const BaseAST& node, const char* expected_name)
    {
        const auto* p = dynamic_cast<const T*>(&node);
        if (p == nullptr) {
            throw std::runtime_error(std::string("AST type mismatch, expected: ") + expected_name);
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
        case BinaryOp::LAND:
            return a && b;
        case BinaryOp::LOR:
            return a || b;
        }
        throw std::runtime_error("unsupported BinaryOp for const eval");
    }

} // namespace

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

IRModule RewindIRBuilder::build(const BaseAST& ast)
{
    IRModule module {};
    const auto& comp_unit = expect_node<CompUnitAST>(ast, "CompUnitAST");
    lower_comp_unit(comp_unit, module);
    return module;
}

void RewindIRBuilder::lower_comp_unit(const CompUnitAST& ast, IRModule& module)
{
    const auto& func_def = expect_node<FuncDefAST>(*ast.func_def, "FuncDefAST");
    lower_func_def(func_def, module);
}

IRFunction* RewindIRBuilder::lower_func_def(const FuncDefAST& ast, IRModule& module)
{
    const auto& func_type = expect_node<FuncTypeAST>(*ast.func_type, "FuncTypeAST");
    auto type = lower_func_type(func_type, module);
    auto func = module.make_function(type, ast.ident);

    // 进入函数作用域
    enter_scope();

    const auto& block = expect_node<BlockAST>(*ast.block, "BlockAST");
    auto basic_block = module.make_basic_block("%entry");
    lower_block(block, module, basic_block);

    module.append_basic_block(*func, *basic_block);

    // 退出函数作用域
    exit_scope();

    return func;
}

IRValueType RewindIRBuilder::lower_func_type(const FuncTypeAST& ast, IRModule& module) const
{
    if (ast.type == "int") {
        return IRValueType::INT32;
    } else if (ast.type == "void") {
        return IRValueType::UNIT;
    }
    throw std::runtime_error("unsupported FuncTypeAST");
}

void RewindIRBuilder::lower_block(const BlockAST& ast, IRModule& module, IRBasicBlock* block)
{
    for (const auto& item : ast.items) {
        if (auto* stmt = dynamic_cast<StmtAST*>(item.get())) {
            lower_stmt(*stmt, module, block);
        }
        if (auto* decl = dynamic_cast<DeclAST*>(item.get())) {
            // 处理常量声明：求值并存储到符号表，不生成 IR
            const auto& const_decl = expect_node<ConstDeclAST>(*decl->const_decl, "ConstDeclAST");
            for (const auto& def_base : const_decl.const_defs) {
                const auto& def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
                const auto& init = expect_node<ConstInitValAST>(*def.const_init_val, "ConstInitValAST");
                const auto& exp = expect_node<ExpAST>(*init.const_exp, "ExpAST");

                // 求值常量表达式
                int32_t value = eval_exp(exp, module);

                // 定义常量
                define_const(def.ident, value);
            }
        }
    }
}

void RewindIRBuilder::lower_stmt(const StmtAST& ast, IRModule& module, IRBasicBlock* block)
{
    std::visit(overloaded {
                   [&](const StmtAST::Return& s) {
                       const auto& exp = expect_node<ExpAST>(*s.exp, "ExpAST");
                       auto value = lower_exp(exp, module);
                       auto ret_inst = module.make_value<IRReturnInst>(value);
                       module.append_inst(*block, *ret_inst);
                   },
                   [&](const auto& other) {
                       std::string type_name = typeid(other).name();
                       throw std::runtime_error("Unsupported statement type: " + type_name);
                   } },
        ast.payload);
}

// ==================== 常量表达式求值（不生成 IR） ====================
void RewindIRBuilder::enter_scope()
{
    const_scopes_.emplace_back();
}

void RewindIRBuilder::exit_scope()
{
    if (const_scopes_.empty()) {
        throw std::runtime_error("exit_scope: no scope to exit");
    }
    const_scopes_.pop_back();
}

void RewindIRBuilder::define_const(const std::string& name, int32_t value)
{
    auto& scope = const_scopes_.back();
    if (scope.count(name)) {
        throw std::runtime_error("redefinition of const: " + name);
    }
    scope[name] = value;
}

std::optional<int32_t> RewindIRBuilder::lookup_const(const std::string& name) const
{
    for (auto it = const_scopes_.rbegin(); it != const_scopes_.rend(); ++it) {
        if (auto found = it->find(name); found != it->end()) {
            return found->second;
        }
    }
    return std::nullopt;
}

int32_t RewindIRBuilder::eval_exp(const ExpAST& ast, IRModule& module)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return eval_lor_exp(lor_exp, module);
}

int32_t RewindIRBuilder::eval_lor_exp(const LOrExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, LOrExpAST::Simple>) {
            const auto& land_exp = expect_node<LAndExpAST>(*v.land_exp, "LAndExpAST");
            return eval_land_exp(land_exp, module);
        } else if constexpr (std::is_same_v<T, LOrExpAST::Binary>) {
            const auto& lor_exp = expect_node<LOrExpAST>(*v.lor_exp, "LOrExpAST");
            const auto& land_exp = expect_node<LAndExpAST>(*v.land_exp, "LAndExpAST");
            auto lhs = eval_lor_exp(lor_exp, module);
            auto rhs = eval_land_exp(land_exp, module);
            return lhs || rhs;
        }
        throw std::runtime_error("invalid LOrExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_land_exp(const LAndExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, LAndExpAST::Simple>) {
            const auto& eq_exp = expect_node<EqExpAST>(*v.eq_exp, "EqExpAST");
            return eval_eq_exp(eq_exp, module);
        } else if constexpr (std::is_same_v<T, LAndExpAST::Binary>) {
            const auto& land_exp = expect_node<LAndExpAST>(*v.land_exp, "LAndExpAST");
            const auto& eq_exp = expect_node<EqExpAST>(*v.eq_exp, "EqExpAST");
            auto lhs = eval_land_exp(land_exp, module);
            auto rhs = eval_eq_exp(eq_exp, module);
            return lhs && rhs;
        }
        throw std::runtime_error("invalid LAndExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_eq_exp(const EqExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, EqExpAST::Simple>) {
            const auto& rel_exp = expect_node<RelExpAST>(*v.rel_exp, "RelExpAST");
            return eval_rel_exp(rel_exp, module);
        } else if constexpr (std::is_same_v<T, EqExpAST::Binary>) {
            const auto& eq_exp = expect_node<EqExpAST>(*v.eq_exp, "EqExpAST");
            const auto& rel_exp = expect_node<RelExpAST>(*v.rel_exp, "RelExpAST");
            auto lhs = eval_eq_exp(eq_exp, module);
            auto rhs = eval_rel_exp(rel_exp, module);
            return lhs == rhs;
        }
        throw std::runtime_error("invalid EqExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_rel_exp(const RelExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RelExpAST::Simple>) {
            const auto& add_exp = expect_node<AddExpAST>(*v.add_exp, "AddExpAST");
            return eval_add_exp(add_exp, module);
        } else if constexpr (std::is_same_v<T, RelExpAST::Binary>) {
            const auto& rel_exp = expect_node<RelExpAST>(*v.rel_exp, "RelExpAST");
            const auto& add_exp = expect_node<AddExpAST>(*v.add_exp, "AddExpAST");
            auto lhs = eval_rel_exp(rel_exp, module);
            auto rhs = eval_add_exp(add_exp, module);
            return eval_binary_op(v.op, lhs, rhs);
        }
        throw std::runtime_error("invalid RelExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_add_exp(const AddExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, AddExpAST::Simple>) {
            const auto& mul_exp = expect_node<MulExpAST>(*v.mul_exp, "MulExpAST");
            return eval_mul_exp(mul_exp, module);
        } else if constexpr (std::is_same_v<T, AddExpAST::Binary>) {
            const auto& add_exp = expect_node<AddExpAST>(*v.add_exp, "AddExpAST");
            const auto& mul_exp = expect_node<MulExpAST>(*v.mul_exp, "MulExpAST");
            auto lhs = eval_add_exp(add_exp, module);
            auto rhs = eval_mul_exp(mul_exp, module);
            return eval_binary_op(v.op, lhs, rhs);
        }
        throw std::runtime_error("invalid AddExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_mul_exp(const MulExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, MulExpAST::Simple>) {
            const auto& unary_exp = expect_node<UnaryExpAST>(*v.unary_exp, "UnaryExpAST");
            return eval_unary_exp(unary_exp, module);
        } else if constexpr (std::is_same_v<T, MulExpAST::Binary>) {
            const auto& mul_exp = expect_node<MulExpAST>(*v.mul_exp, "MulExpAST");
            const auto& unary_exp = expect_node<UnaryExpAST>(*v.unary_exp, "UnaryExpAST");
            auto lhs = eval_mul_exp(mul_exp, module);
            auto rhs = eval_unary_exp(unary_exp, module);
            return eval_binary_op(v.op, lhs, rhs);
        }
        throw std::runtime_error("invalid MulExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_unary_exp(const UnaryExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, UnaryExpAST::Primary>) {
            const auto& primary = expect_node<PrimaryExpAST>(*v.exp, "PrimaryExpAST");
            return eval_primary_exp(primary, module);
        } else if constexpr (std::is_same_v<T, UnaryExpAST::Unary>) {
            const auto& primary = expect_node<PrimaryExpAST>(*v.exp, "PrimaryExpAST");
            auto operand = eval_primary_exp(primary, module);
            switch (v.op) {
            case UnaryOp::PLUS:
                return +operand;
            case UnaryOp::MINUS:
                return -operand;
            case UnaryOp::NOT:
                return !operand;
            }
        }
        throw std::runtime_error("invalid UnaryExpAST payload");
    },
        ast.payload);
}

int32_t RewindIRBuilder::eval_primary_exp(const PrimaryExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> int32_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PrimaryExpAST::Number>) {
            return v.value;
        } else if constexpr (std::is_same_v<T, PrimaryExpAST::Expression>) {
            const auto& exp = expect_node<ExpAST>(*v.exp, "ExpAST");
            return eval_exp(exp, module);
        } else if constexpr (std::is_same_v<T, PrimaryExpAST::LValue>) {
            // 关键：查符号表获取常量值
            auto sym = lookup_const(v.ident);
            if (!sym) {
                throw std::runtime_error("undefined const: " + v.ident);
            }
            return *sym;
        }
        throw std::runtime_error("invalid PrimaryExpAST payload");
    },
        ast.payload);
}

// ==================== IR 生成（使用常量折叠后的值） ====================

IRValue* RewindIRBuilder::lower_exp(const ExpAST& ast, IRModule& module)
{
    const auto& lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
    return lower_lor_exp(lor_exp, module);
}

IRValue* RewindIRBuilder::lower_lor_exp(const LOrExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, LOrExpAST::Simple>) {
            const auto& land_exp = expect_node<LAndExpAST>(*v.land_exp, "LAndExpAST");
            return lower_land_exp(land_exp, module);
        } else if constexpr (std::is_same_v<T, LOrExpAST::Binary>) {
            const auto& lor_exp = expect_node<LOrExpAST>(*v.lor_exp, "LOrExpAST");
            const auto& land_exp = expect_node<LAndExpAST>(*v.land_exp, "LAndExpAST");

            auto lhs = lower_lor_exp(lor_exp, module);
            auto rhs = lower_land_exp(land_exp, module);

            auto or_value = module.make_value<IRBinaryInst>(IRBinaryOp::OR, lhs, rhs);
            auto zero = module.make_value<IRConstant>(0);
            auto value = module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, or_value, zero);

            return value;
        }
        throw std::runtime_error("invalid LOrExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_land_exp(const LAndExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, LAndExpAST::Simple>) {
            const auto& eq_exp = expect_node<EqExpAST>(*v.eq_exp, "EqExpAST");
            return lower_eq_exp(eq_exp, module);
        } else if constexpr (std::is_same_v<T, LAndExpAST::Binary>) {
            const auto& land_exp = expect_node<LAndExpAST>(*v.land_exp, "LAndExpAST");
            const auto& eq_exp = expect_node<EqExpAST>(*v.eq_exp, "EqExpAST");

            auto lhs = lower_land_exp(land_exp, module);
            auto rhs = lower_eq_exp(eq_exp, module);

            auto zero = module.make_value<IRConstant>(0);
            auto nlhs = module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, lhs, zero);
            auto nrhs = module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, rhs, zero);
            auto value = module.make_value<IRBinaryInst>(IRBinaryOp::AND, nlhs, nrhs);

            return value;
        }
        throw std::runtime_error("invalid LAndExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_eq_exp(const EqExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, EqExpAST::Simple>) {
            const auto& rel_exp = expect_node<RelExpAST>(*v.rel_exp, "RelExpAST");
            return lower_rel_exp(rel_exp, module);
        } else if constexpr (std::is_same_v<T, EqExpAST::Binary>) {
            const auto& eq_exp = expect_node<EqExpAST>(*v.eq_exp, "EqExpAST");
            const auto& rel_exp = expect_node<RelExpAST>(*v.rel_exp, "RelExpAST");

            auto lhs = lower_eq_exp(eq_exp, module);
            auto rhs = lower_rel_exp(rel_exp, module);

            auto value = module.make_value<IRBinaryInst>(IRBinaryOp::EQ, lhs, rhs);
            return value;
        }
        throw std::runtime_error("invalid EqExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_rel_exp(const RelExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, RelExpAST::Simple>) {
            const auto& add_exp = expect_node<AddExpAST>(*v.add_exp, "AddExpAST");
            return lower_add_exp(add_exp, module);
        } else if constexpr (std::is_same_v<T, RelExpAST::Binary>) {
            const auto& rel_exp = expect_node<RelExpAST>(*v.rel_exp, "RelExpAST");
            const auto& add_exp = expect_node<AddExpAST>(*v.add_exp, "AddExpAST");

            auto lhs = lower_rel_exp(rel_exp, module);
            auto rhs = lower_add_exp(add_exp, module);

            auto op = ast_op_to_ir_op(v.op);
            auto value = module.make_value<IRBinaryInst>(op, lhs, rhs);
            return value;
        }
        throw std::runtime_error("invalid RelExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_add_exp(const AddExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, AddExpAST::Simple>) {
            const auto& mul_exp = expect_node<MulExpAST>(*v.mul_exp, "MulExpAST");
            return lower_mul_exp(mul_exp, module);
        } else if constexpr (std::is_same_v<T, AddExpAST::Binary>) {
            const auto& add_exp = expect_node<AddExpAST>(*v.add_exp, "AddExpAST");
            const auto& mul_exp = expect_node<MulExpAST>(*v.mul_exp, "MulExpAST");

            auto lhs = lower_add_exp(add_exp, module);
            auto rhs = lower_mul_exp(mul_exp, module);

            auto op = ast_op_to_ir_op(v.op);
            auto value = module.make_value<IRBinaryInst>(op, lhs, rhs);
            return value;
        }
        throw std::runtime_error("invalid AddExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_mul_exp(const MulExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, MulExpAST::Simple>) {
            const auto& unary_exp = expect_node<UnaryExpAST>(*v.unary_exp, "UnaryExpAST");
            return lower_unary_exp(unary_exp, module);
        } else if constexpr (std::is_same_v<T, MulExpAST::Binary>) {
            const auto& mul_exp = expect_node<MulExpAST>(*v.mul_exp, "MulExpAST");
            const auto& unary_exp = expect_node<UnaryExpAST>(*v.unary_exp, "UnaryExpAST");

            auto lhs = lower_mul_exp(mul_exp, module);
            auto rhs = lower_unary_exp(unary_exp, module);

            auto op = ast_op_to_ir_op(v.op);
            auto value = module.make_value<IRBinaryInst>(op, lhs, rhs);
            return value;
        }
        throw std::runtime_error("invalid MulExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_unary_exp(const UnaryExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, UnaryExpAST::Primary>) {
            const auto& primary = expect_node<PrimaryExpAST>(*v.exp, "PrimaryExpAST");
            return lower_primary_exp(primary, module);
        } else if constexpr (std::is_same_v<T, UnaryExpAST::Unary>) {
            const auto& primary = expect_node<PrimaryExpAST>(*v.exp, "PrimaryExpAST");
            auto operand = lower_primary_exp(primary, module);

            // 一元运算符转换为二元运算
            auto zero = module.make_value<IRConstant>(0);
            switch (v.op) {
            case UnaryOp::PLUS:
                return operand; // +x = x
            case UnaryOp::MINUS:
                return module.make_value<IRBinaryInst>(IRBinaryOp::SUB, zero, operand);
            case UnaryOp::NOT:
                return module.make_value<IRBinaryInst>(IRBinaryOp::EQ, operand, zero);
            }
        }
        throw std::runtime_error("invalid UnaryExpAST payload");
    },
        ast.payload);
}

IRValue* RewindIRBuilder::lower_primary_exp(const PrimaryExpAST& ast, IRModule& module)
{
    return std::visit([&](const auto& v) -> IRValue* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PrimaryExpAST::Number>) {
            return module.make_value<IRConstant>(v.value);
        } else if constexpr (std::is_same_v<T, PrimaryExpAST::Expression>) {
            const auto& exp = expect_node<ExpAST>(*v.exp, "ExpAST");
            return lower_exp(exp, module);
        } else if constexpr (std::is_same_v<T, PrimaryExpAST::LValue>) {
            // 关键：查符号表获取常量值，如果找到则内联
            auto sym = lookup_const(v.ident);
            if (sym) {
                // 常量：直接生成 Integer
                return module.make_value<IRConstant>(*sym);
            }
            // 变量：当前 SysY 不支持非 const 变量，报错
            throw std::runtime_error("undefined identifier: " + v.ident);
        }
        throw std::runtime_error("invalid PrimaryExpAST payload");
    },
        ast.payload);
}

} // namespace rewind_ir
