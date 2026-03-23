#pragma once
#include <iostream>
#include <memory>
#include <string>

enum class PrimaryExpKind {
    EXP,
    NUM
};

enum class UnaryExpKind {
    PRIMARY,
    OP
};

enum class StmtKind {
    RETURN
};

namespace ast_dump_detail {

inline std::string indent(int n)
{
    return std::string(n, ' ');
}

inline const char* primary_exp_kind_to_cstr(PrimaryExpKind kind)
{
    switch (kind) {
    case PrimaryExpKind::EXP:
        return "EXP";
    case PrimaryExpKind::NUM:
        return "NUM";
    default:
        return "UNKNOWN";
    }
}

inline const char* unary_exp_kind_to_cstr(UnaryExpKind kind)
{
    switch (kind) {
    case UnaryExpKind::PRIMARY:
        return "PRIMARY";
    case UnaryExpKind::OP:
        return "OP";
    default:
        return "UNKNOWN";
    }
}

inline const char* stmt_kind_to_cstr(StmtKind kind)
{
    switch (kind) {
    case StmtKind::RETURN:
        return "RETURN";
    default:
        return "UNKNOWN";
    }
}

} // namespace ast_dump_detail

// 所有AST的基类
class BaseAST {
public:
    virtual ~BaseAST() = default;

    virtual void Dump(int indent = 0) const = 0;
};

// CompUnit 是 BaseAST
class CompUnitAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> func_def;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "CompUnitAST {\n";
        if (func_def) {
            func_def->Dump(indent + 2);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

// FuncDef 也是 BaseAST
class FuncDefAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> func_type;
    std::string ident;
    std::unique_ptr<BaseAST> block;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "FuncDefAST {\n";
        if (func_type) {
            std::cout << ast_dump_detail::indent(indent + 2) << "func_type:\n";
            func_type->Dump(indent + 4);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent + 2) << "ident: " << ident << "\n";
        if (block) {
            std::cout << ast_dump_detail::indent(indent + 2) << "block:\n";
            block->Dump(indent + 4);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class FuncTypeAST : public BaseAST {
public:
    std::string type;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "FuncTypeAST { type: " << type << " }";
    }
};

class BlockAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> stmt;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "BlockAST {\n";
        if (stmt) {
            stmt->Dump(indent + 2);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class StmtAST : public BaseAST {
public:
    StmtKind kind { StmtKind::RETURN };
    std::unique_ptr<BaseAST> return_exp;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "StmtAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::stmt_kind_to_cstr(kind) << "\n";
        if (kind == StmtKind::RETURN) {
            std::cout << ast_dump_detail::indent(indent + 2) << "return_exp:\n";
        }
        if (return_exp) {
            return_exp->Dump(indent + 4);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class ExpAST : public BaseAST {
public:
    std::unique_ptr<BaseAST> unary_exp;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "ExpAST {\n";
        if (unary_exp) {
            unary_exp->Dump(indent + 2);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class UnaryExpAST : public BaseAST {
public:
    UnaryExpKind epk;
    std::unique_ptr<BaseAST> exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "UnaryExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::unary_exp_kind_to_cstr(epk);
        if (epk == UnaryExpKind::OP) {
            std::cout << "\n"
                      << ast_dump_detail::indent(indent + 2) << "op: " << op;
        }
        std::cout << "\n";
        if (exp) {
            exp->Dump(indent + 2);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class PrimaryExpAST : public BaseAST {
public:
    PrimaryExpKind epk;
    int number;
    std::unique_ptr<BaseAST> exp;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "PrimaryExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::primary_exp_kind_to_cstr(epk) << "\n";
        if (epk == PrimaryExpKind::NUM) {
            std::cout << ast_dump_detail::indent(indent + 2) << "number: " << number << "\n";
        } else if (exp) {
            exp->Dump(indent + 2);
            std::cout << "\n";
        }
        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};