#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class UnaryOp {
    PLUS,
    MINUS,
    NOT
};

enum class BinaryOp {
    MUL,
    DIV,
    MOD,
    ADD,
    SUB,
    EQ,
    NEQ,
    LT, // 小于
    GT, // 大于
    LE, // 小于等于
    GE, // 大于等于
    LAND,
    LOR
};

enum class BType {
    INT
};

namespace ast_dump_detail {

struct IndentToken {
    int n;
};

IndentToken indent(int n);
std::ostream& operator<<(std::ostream& out, IndentToken token);

std::string_view unary_op_to_cstr(UnaryOp op);
std::string_view binary_op_to_cstr(BinaryOp op);
std::string_view btype_to_cstr(BType type);

} // namespace ast_dump_detail

class BaseAST;
class CompUnitAST;
class FuncDefAST;
class FuncTypeAST;
class BlockAST;
class StmtAST;
class ExpAST;
class UnaryExpAST;
class PrimaryExpAST;
class AddExpAST;
class MulExpAST;
class RelExpAST;
class EqExpAST;
class LAndExpAST;
class LOrExpAST;
class DeclAST;
class ConstDeclAST;
class ConstDefAST;
class ConstInitValAST;

// 所有 AST 的基类
class BaseAST {
public:
    virtual ~BaseAST() = default;

    virtual void Dump(std::ostream& out, int indent = 0) const = 0;
};

// CompUnitAST
class CompUnitAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> func_def;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// FuncDefAST
class FuncDefAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> func_type;
    std::string ident;
    std::unique_ptr<BaseAST> block;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// FuncTypeAST
class FuncTypeAST : public BaseAST {
public:
    std::string type;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// DeclAST
class DeclAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> const_decl;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// ConstDeclAST
class ConstDeclAST : public BaseAST {
public:
    BType type = BType::INT;
    std::vector<std::unique_ptr<BaseAST>> const_defs;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// ConstDefAST
class ConstDefAST : public BaseAST {
public:
    std::string ident;
    std::unique_ptr<BaseAST> const_init_val;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// ConstInitValAST
class ConstInitValAST : public BaseAST {
public:
    // 注意：AST中没有给出ConstExpAST类，const_exp实际类型就是ExpAST
    std::unique_ptr<BaseAST> const_exp;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// BlockAST
class BlockAST : public BaseAST {
public:
    std::vector<std::unique_ptr<BaseAST>> items;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// StmtAST - 使用 variant 替代 kind 字段
class StmtAST : public BaseAST {
public:
    struct Return {
        std::unique_ptr<BaseAST> exp;
    };

    using Payload = std::variant<Return>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// ExpAST
class ExpAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> lor_exp;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// LOrExpAST - 使用 variant 替代 epk 字段
class LOrExpAST : public BaseAST {
public:
    struct Simple {
        std::unique_ptr<BaseAST> land_exp;
    };
    struct Binary {
        BinaryOp op = BinaryOp::LOR;
        std::unique_ptr<BaseAST> lor_exp;
        std::unique_ptr<BaseAST> land_exp;
    };

    using Payload = std::variant<Simple, Binary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// LAndExpAST - 使用 variant 替代 epk 字段
class LAndExpAST : public BaseAST {
public:
    struct Simple {
        std::unique_ptr<BaseAST> eq_exp;
    };
    struct Binary {
        BinaryOp op = BinaryOp::LAND;
        std::unique_ptr<BaseAST> land_exp;
        std::unique_ptr<BaseAST> eq_exp;
    };

    using Payload = std::variant<Simple, Binary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// EqExpAST - 使用 variant 替代 epk 字段
class EqExpAST : public BaseAST {
public:
    struct Simple {
        std::unique_ptr<BaseAST> rel_exp;
    };
    struct Binary {
        BinaryOp op = BinaryOp::EQ;
        std::unique_ptr<BaseAST> eq_exp;
        std::unique_ptr<BaseAST> rel_exp;
    };

    using Payload = std::variant<Simple, Binary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// RelExpAST - 使用 variant 替代 epk 字段
class RelExpAST : public BaseAST {
public:
    struct Simple {
        std::unique_ptr<BaseAST> add_exp;
    };
    struct Binary {
        BinaryOp op = BinaryOp::LT;
        std::unique_ptr<BaseAST> rel_exp;
        std::unique_ptr<BaseAST> add_exp;
    };

    using Payload = std::variant<Simple, Binary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// AddExpAST - 使用 variant 替代 epk 字段
class AddExpAST : public BaseAST {
public:
    struct Simple {
        std::unique_ptr<BaseAST> mul_exp;
    };
    struct Binary {
        BinaryOp op = BinaryOp::ADD;
        std::unique_ptr<BaseAST> add_exp;
        std::unique_ptr<BaseAST> mul_exp;
    };

    using Payload = std::variant<Simple, Binary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// MulExpAST - 使用 variant 替代 epk 字段
class MulExpAST : public BaseAST {
public:
    struct Simple {
        std::unique_ptr<BaseAST> unary_exp;
    };
    struct Binary {
        BinaryOp op = BinaryOp::MUL;
        std::unique_ptr<BaseAST> mul_exp;
        std::unique_ptr<BaseAST> unary_exp;
    };

    using Payload = std::variant<Simple, Binary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// UnaryExpAST
class UnaryExpAST : public BaseAST {
public:
    struct Primary {
        std::unique_ptr<BaseAST> exp;
    };
    struct Unary {
        UnaryOp op;
        std::unique_ptr<BaseAST> exp;
    };

    using Payload = std::variant<Primary, Unary>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};

// PrimaryExpAST - 使用 variant，内联 LVal
class PrimaryExpAST : public BaseAST {
public:
    struct Number {
        int value;
    };
    struct Expression {
        std::unique_ptr<BaseAST> exp;
    };
    struct LValue {
        std::string ident;
    };

    using Payload = std::variant<Number, Expression, LValue>;
    Payload payload;

    void Dump(std::ostream& out, int indent = 0) const override;
};