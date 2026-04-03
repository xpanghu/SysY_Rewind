#include "ir/rewind_ir_builder.h"
#include "front_end/ast.h"
#include "ir/symbol_table.h"

namespace rewind_ir {
namespace {

template <typename T>
const T &expect_node(const BaseAST &node, const char *expected_name) {
  const auto *p = dynamic_cast<const T *>(&node);
  if (p == nullptr) {
    throw std::runtime_error(std::string("AST type mismatch, expected: ") +
                             expected_name);
  }
  return *p;
}

inline IRBinaryOp ast_op_to_ir_op(BinaryOp op) {
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

inline int32_t eval_binary_op(BinaryOp op, int32_t a, int32_t b) {
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

} // namespace

// 定义了 overloaded 结构体，配套 std::variant 使用
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

IRModule RewindIRBuilder::build(const BaseAST &ast) {
  IRModule module{};
  const auto &comp_unit = expect_node<CompUnitAST>(ast, "CompUnitAST");
  lower_comp_unit(comp_unit, module);
  return module;
}

int32_t RewindIRBuilder::eval_exp(const ExpAST &ast, IRModule &module) {
  const auto &lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
  return eval_lor_exp(lor_exp, module);
}

int32_t RewindIRBuilder::eval_lor_exp(const LOrExpAST &ast, IRModule &module) {
  return std::visit(
      overloaded{[&](const LOrExpAST::Simple &s) -> int32_t {
                   const auto &land_exp =
                       expect_node<LAndExpAST>(*s.land_exp, "LAndExpAST");
                   return eval_land_exp(land_exp, module);
                 },
                 [&](const LOrExpAST::Binary &b) -> int32_t {
                   const auto &lor_exp =
                       expect_node<LOrExpAST>(*b.lor_exp, "LOrExpAST");
                   const auto &land_exp =
                       expect_node<LAndExpAST>(*b.land_exp, "LAndExpAST");
                   auto lhs = eval_lor_exp(lor_exp, module);
                   auto rhs = eval_land_exp(land_exp, module);
                   return lhs || rhs;
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_land_exp(const LAndExpAST &ast,
                                       IRModule &module) {
  return std::visit(
      overloaded{[&](const LAndExpAST::Simple &s) -> int32_t {
                   const auto &eq_exp =
                       expect_node<EqExpAST>(*s.eq_exp, "EqExpAST");
                   return eval_eq_exp(eq_exp, module);
                 },
                 [&](const LAndExpAST::Binary &b) -> int32_t {
                   const auto &land_exp =
                       expect_node<LAndExpAST>(*b.land_exp, "LAndExpAST");
                   const auto &eq_exp =
                       expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");
                   auto lhs = eval_land_exp(land_exp, module);
                   auto rhs = eval_eq_exp(eq_exp, module);
                   return lhs && rhs;
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_eq_exp(const EqExpAST &ast, IRModule &module) {
  return std::visit(
      overloaded{[&](const EqExpAST::Simple &s) -> int32_t {
                   const auto &rel_exp =
                       expect_node<RelExpAST>(*s.rel_exp, "RelExpAST");
                   return eval_rel_exp(rel_exp, module);
                 },
                 [&](const EqExpAST::Binary &b) -> int32_t {
                   const auto &eq_exp =
                       expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");
                   const auto &rel_exp =
                       expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                   auto lhs = eval_eq_exp(eq_exp, module);
                   auto rhs = eval_rel_exp(rel_exp, module);
                   return eval_binary_op(b.op, lhs, rhs);
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_rel_exp(const RelExpAST &ast, IRModule &module) {
  return std::visit(
      overloaded{[&](const RelExpAST::Simple &s) -> int32_t {
                   const auto &add_exp =
                       expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                   return eval_add_exp(add_exp, module);
                 },
                 [&](const RelExpAST::Binary &b) -> int32_t {
                   const auto &rel_exp =
                       expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                   const auto &add_exp =
                       expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                   auto lhs = eval_rel_exp(rel_exp, module);
                   auto rhs = eval_add_exp(add_exp, module);
                   return eval_binary_op(b.op, lhs, rhs);
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_add_exp(const AddExpAST &ast, IRModule &module) {
  return std::visit(
      overloaded{[&](const AddExpAST::Simple &s) -> int32_t {
                   const auto &mul_exp =
                       expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                   return eval_mul_exp(mul_exp, module);
                 },
                 [&](const AddExpAST::Binary &b) -> int32_t {
                   const auto &add_exp =
                       expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                   const auto &mul_exp =
                       expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                   auto lhs = eval_add_exp(add_exp, module);
                   auto rhs = eval_mul_exp(mul_exp, module);
                   return eval_binary_op(b.op, lhs, rhs);
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_mul_exp(const MulExpAST &ast, IRModule &module) {
  return std::visit(
      overloaded{[&](const MulExpAST::Simple &s) -> int32_t {
                   const auto &unary_exp =
                       expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                   return eval_unary_exp(unary_exp, module);
                 },
                 [&](const MulExpAST::Binary &b) -> int32_t {
                   const auto &mul_exp =
                       expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                   const auto &unary_exp =
                       expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");
                   auto lhs = eval_mul_exp(mul_exp, module);
                   auto rhs = eval_unary_exp(unary_exp, module);
                   return eval_binary_op(b.op, lhs, rhs);
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_unary_exp(const UnaryExpAST &ast,
                                        IRModule &module) {
  return std::visit(
      overloaded{[&](const UnaryExpAST::Primary &p) -> int32_t {
                   const auto &primary =
                       expect_node<PrimaryExpAST>(*p.exp, "PrimaryExpAST");
                   return eval_primary_exp(primary, module);
                 },
                 [&](const UnaryExpAST::Unary &u) -> int32_t {
                   const auto &primary =
                       expect_node<PrimaryExpAST>(*u.exp, "PrimaryExpAST");
                   auto operand = eval_primary_exp(primary, module);
                   switch (u.op) {
                   case UnaryOp::PLUS:
                     return +operand;
                   case UnaryOp::MINUS:
                     return -operand;
                   case UnaryOp::NOT:
                     return !operand;
                   }
                   throw std::runtime_error("invalid UnaryOp");
                 }},
      ast.payload);
}

int32_t RewindIRBuilder::eval_primary_exp(const PrimaryExpAST &ast,
                                          IRModule &module) {
  return std::visit(
      overloaded{
          [&](const PrimaryExpAST::Number &n) -> int32_t { return n.value; },
          [&](const PrimaryExpAST::Expression &e) -> int32_t {
            const auto &exp = expect_node<ExpAST>(*e.exp, "ExpAST");
            return eval_exp(exp, module);
          },
          [&](const PrimaryExpAST::LValue &l) -> int32_t {
            auto sym = symbol_table_.lookup_const(l.ident);
            if (!sym) {
              throw std::runtime_error("undefined const: " + l.ident);
            }
            return *sym;
          }},
      ast.payload);
}

// ==================== IR 生成（使用常量折叠后的值） ====================
void RewindIRBuilder::lower_comp_unit(const CompUnitAST &ast,
                                      IRModule &module) {
  const auto &func_def = expect_node<FuncDefAST>(*ast.func_def, "FuncDefAST");
  lower_func_def(func_def, module);
}

IRFunction *RewindIRBuilder::lower_func_def(const FuncDefAST &ast,
                                            IRModule &module) {
  const auto &func_type =
      expect_node<FuncTypeAST>(*ast.func_type, "FuncTypeAST");
  auto type = lower_func_type(func_type, module);
  auto func = module.make_function(type, ast.ident);

  // 进入函数作用域
  symbol_table_.enter_scope();

  const auto &block = expect_node<BlockAST>(*ast.block, "BlockAST");
  auto basic_block = module.make_basic_block("%entry");
  lower_block(block, module, basic_block);

  module.append_basic_block(*func, *basic_block);

  // 退出函数作用域
  symbol_table_.exit_scope();

  return func;
}

IRValueType RewindIRBuilder::lower_func_type(const FuncTypeAST &ast,
                                             IRModule &module) const {
  if (ast.type == "int") {
    return IRValueType::INT32;
  } else if (ast.type == "void") {
    return IRValueType::UNIT;
  }
  throw std::runtime_error("unsupported FuncTypeAST");
}

void RewindIRBuilder::lower_block(const BlockAST &ast, IRModule &module,
                                  IRBasicBlock *block) {
  for (const auto &item : ast.items) {
    if (auto *stmt = dynamic_cast<StmtAST *>(item.get())) {
      lower_stmt(*stmt, module, block);
    }
    // 处理常量和变量
    if (auto *decl = dynamic_cast<DeclAST *>(item.get())) {
      // 处理常量声明：求值并存储到符号表，不生成 IR
      const auto &const_decl =
          expect_node<ConstDeclAST>(*decl->const_decl, "ConstDeclAST");
      for (const auto &def_base : const_decl.const_defs) {
        const auto &def = expect_node<ConstDefAST>(*def_base, "ConstDefAST");
        const auto &init = expect_node<ConstInitValAST>(*def.const_init_val,
                                                        "ConstInitValAST");
        const auto &exp = expect_node<ExpAST>(*init.const_exp, "ExpAST");

        // 求值常量表达式
        int32_t value = eval_exp(exp, module);

        // 定义常量
        symbol_table_.define_const(def.ident, value);
      }
    }
  }
}

void RewindIRBuilder::lower_stmt(const StmtAST &ast, IRModule &module,
                                 IRBasicBlock *block) {
  std::visit(
      overloaded{[&](const StmtAST::Return &s) {
                   const auto &exp = expect_node<ExpAST>(*s.exp, "ExpAST");
                   auto value = lower_exp(exp, module);
                   auto ret_inst = module.make_value<IRReturnInst>(value);
                   module.append_inst(*block, *ret_inst);
                 },
                 [&](const auto &other) {
                   std::string type_name = typeid(other).name();
                   throw std::runtime_error("Unsupported statement type: " +
                                            type_name);
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_exp(const ExpAST &ast, IRModule &module) {
  const auto &lor_exp = expect_node<LOrExpAST>(*ast.lor_exp, "LOrExpAST");
  return lower_lor_exp(lor_exp, module);
}

IRValue *RewindIRBuilder::lower_lor_exp(const LOrExpAST &ast,
                                        IRModule &module) {
  return std::visit(
      overloaded{[&](const LOrExpAST::Simple &s) -> IRValue * {
                   const auto &land_exp =
                       expect_node<LAndExpAST>(*s.land_exp, "LAndExpAST");
                   return lower_land_exp(land_exp, module);
                 },
                 [&](const LOrExpAST::Binary &b) -> IRValue * {
                   const auto &lor_exp =
                       expect_node<LOrExpAST>(*b.lor_exp, "LOrExpAST");
                   const auto &land_exp =
                       expect_node<LAndExpAST>(*b.land_exp, "LAndExpAST");

                   auto lhs = lower_lor_exp(lor_exp, module);
                   auto rhs = lower_land_exp(land_exp, module);

                   auto or_value = module.make_value<IRBinaryInst>(
                       IRBinaryOp::OR, lhs, rhs);
                   auto zero = get_or_create_constant(0, module);
                   auto value = module.make_value<IRBinaryInst>(IRBinaryOp::NEQ,
                                                                or_value, zero);

                   return value;
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_land_exp(const LAndExpAST &ast,
                                         IRModule &module) {
  return std::visit(
      overloaded{
          [&](const LAndExpAST::Simple &s) -> IRValue * {
            const auto &eq_exp = expect_node<EqExpAST>(*s.eq_exp, "EqExpAST");
            return lower_eq_exp(eq_exp, module);
          },
          [&](const LAndExpAST::Binary &b) -> IRValue * {
            const auto &land_exp =
                expect_node<LAndExpAST>(*b.land_exp, "LAndExpAST");
            const auto &eq_exp = expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");

            auto lhs = lower_land_exp(land_exp, module);
            auto rhs = lower_eq_exp(eq_exp, module);

            auto zero = get_or_create_constant(0, module);
            auto nlhs =
                module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, lhs, zero);
            auto nrhs =
                module.make_value<IRBinaryInst>(IRBinaryOp::NEQ, rhs, zero);
            auto value =
                module.make_value<IRBinaryInst>(IRBinaryOp::AND, nlhs, nrhs);

            return value;
          }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_eq_exp(const EqExpAST &ast, IRModule &module) {
  return std::visit(
      overloaded{[&](const EqExpAST::Simple &s) -> IRValue * {
                   const auto &rel_exp =
                       expect_node<RelExpAST>(*s.rel_exp, "RelExpAST");
                   return lower_rel_exp(rel_exp, module);
                 },
                 [&](const EqExpAST::Binary &b) -> IRValue * {
                   const auto &eq_exp =
                       expect_node<EqExpAST>(*b.eq_exp, "EqExpAST");
                   const auto &rel_exp =
                       expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");

                   auto lhs = lower_eq_exp(eq_exp, module);
                   auto rhs = lower_rel_exp(rel_exp, module);

                   auto value = module.make_value<IRBinaryInst>(IRBinaryOp::EQ,
                                                                lhs, rhs);
                   return value;
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_rel_exp(const RelExpAST &ast,
                                        IRModule &module) {
  return std::visit(
      overloaded{[&](const RelExpAST::Simple &s) -> IRValue * {
                   const auto &add_exp =
                       expect_node<AddExpAST>(*s.add_exp, "AddExpAST");
                   return lower_add_exp(add_exp, module);
                 },
                 [&](const RelExpAST::Binary &b) -> IRValue * {
                   const auto &rel_exp =
                       expect_node<RelExpAST>(*b.rel_exp, "RelExpAST");
                   const auto &add_exp =
                       expect_node<AddExpAST>(*b.add_exp, "AddExpAST");

                   auto lhs = lower_rel_exp(rel_exp, module);
                   auto rhs = lower_add_exp(add_exp, module);

                   auto op = ast_op_to_ir_op(b.op);
                   auto value = module.make_value<IRBinaryInst>(op, lhs, rhs);
                   return value;
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_add_exp(const AddExpAST &ast,
                                        IRModule &module) {
  return std::visit(
      overloaded{[&](const AddExpAST::Simple &s) -> IRValue * {
                   const auto &mul_exp =
                       expect_node<MulExpAST>(*s.mul_exp, "MulExpAST");
                   return lower_mul_exp(mul_exp, module);
                 },
                 [&](const AddExpAST::Binary &b) -> IRValue * {
                   const auto &add_exp =
                       expect_node<AddExpAST>(*b.add_exp, "AddExpAST");
                   const auto &mul_exp =
                       expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");

                   auto lhs = lower_add_exp(add_exp, module);
                   auto rhs = lower_mul_exp(mul_exp, module);

                   auto op = ast_op_to_ir_op(b.op);
                   auto value = module.make_value<IRBinaryInst>(op, lhs, rhs);
                   return value;
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_mul_exp(const MulExpAST &ast,
                                        IRModule &module) {
  return std::visit(
      overloaded{[&](const MulExpAST::Simple &s) -> IRValue * {
                   const auto &unary_exp =
                       expect_node<UnaryExpAST>(*s.unary_exp, "UnaryExpAST");
                   return lower_unary_exp(unary_exp, module);
                 },
                 [&](const MulExpAST::Binary &b) -> IRValue * {
                   const auto &mul_exp =
                       expect_node<MulExpAST>(*b.mul_exp, "MulExpAST");
                   const auto &unary_exp =
                       expect_node<UnaryExpAST>(*b.unary_exp, "UnaryExpAST");

                   auto lhs = lower_mul_exp(mul_exp, module);
                   auto rhs = lower_unary_exp(unary_exp, module);

                   auto op = ast_op_to_ir_op(b.op);
                   auto value = module.make_value<IRBinaryInst>(op, lhs, rhs);
                   return value;
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_unary_exp(const UnaryExpAST &ast,
                                          IRModule &module) {
  return std::visit(
      overloaded{[&](const UnaryExpAST::Primary &p) -> IRValue * {
                   const auto &primary =
                       expect_node<PrimaryExpAST>(*p.exp, "PrimaryExpAST");
                   return lower_primary_exp(primary, module);
                 },
                 [&](const UnaryExpAST::Unary &u) -> IRValue * {
                   const auto &primary =
                       expect_node<PrimaryExpAST>(*u.exp, "PrimaryExpAST");
                   auto operand = lower_primary_exp(primary, module);

                   // 一元运算符转换为二元运算
                   auto zero = get_or_create_constant(0, module);
                   switch (u.op) {
                   case UnaryOp::PLUS:
                     return operand; // +x = x
                   case UnaryOp::MINUS:
                     return module.make_value<IRBinaryInst>(IRBinaryOp::SUB,
                                                            zero, operand);
                   case UnaryOp::NOT:
                     return module.make_value<IRBinaryInst>(IRBinaryOp::EQ,
                                                            operand, zero);
                   }
                   throw std::runtime_error("invalid UnaryOp");
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::lower_primary_exp(const PrimaryExpAST &ast,
                                            IRModule &module) {
  return std::visit(
      overloaded{[&](const PrimaryExpAST::Number &n) -> IRValue * {
                   return get_or_create_constant(n.value, module);
                 },
                 [&](const PrimaryExpAST::Expression &e) -> IRValue * {
                   const auto &exp = expect_node<ExpAST>(*e.exp, "ExpAST");
                   return lower_exp(exp, module);
                 },
                 [&](const PrimaryExpAST::LValue &l) -> IRValue * {
                   // 关键：查符号表获取常量值，如果找到则内联
                   auto sym = symbol_table_.lookup_const(l.ident);

                   if (sym) {
                     // 常量：从缓存获取或创建
                     return get_or_create_constant(*sym, module);
                   }
                   // 变量：当前 SysY 不支持非 const 变量，报错
                   throw std::runtime_error("undefined identifier: " + l.ident);
                 }},
      ast.payload);
}

IRValue *RewindIRBuilder::get_or_create_constant(int32_t value,
                                                 IRModule &module) {
  auto it = constant_cache_.find(value);
  if (it != constant_cache_.end()) {
    return it->second;
  }
  auto *c = module.make_value<IRConstant>(value);
  constant_cache_[value] = c;
  return c;
}

} // namespace rewind_ir
