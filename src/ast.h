#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

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

enum class AddExpKind {
    ADDANDMUL,
    MUL
};

enum class MulExpKind {
    UNARY,
    MULANDUNARY
};

enum class RelExpKind {
    ADD,
    RELANDADD
};

enum class LOrExpKind {
    LAND,
    LORANDLAND
};

enum class LAndExpKind {
    EQ,
    LANDANDEQ
};

enum class EqExpKind {
    REL,
    EQANDREL
};

namespace ast_dump_detail {

inline std::string indent(int n)
{
    return std::string(n, ' ');
}

inline std::string_view primary_exp_kind_to_cstr(PrimaryExpKind kind)
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

inline std::string_view unary_exp_kind_to_cstr(UnaryExpKind kind)
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

inline std::string_view stmt_kind_to_cstr(StmtKind kind)
{
    switch (kind) {
    case StmtKind::RETURN:
        return "RETURN";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view add_exp_kind_to_cstr(AddExpKind kind)
{
    switch (kind) {
    case AddExpKind::ADDANDMUL:
        return "ADDANDMUL";
    case AddExpKind::MUL:
        return "MUL";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view mul_exp_kind_to_cstr(MulExpKind kind)
{
    switch (kind) {
    case MulExpKind::UNARY:
        return "UNARY";
    case MulExpKind::MULANDUNARY:
        return "MULANDUNARY";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view rel_exp_kind_to_cstr(RelExpKind kind)
{
    switch (kind) {
    case RelExpKind::ADD:
        return "ADD";
    case RelExpKind::RELANDADD:
        return "RELANDADD";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view lor_exp_kind_to_cstr(LOrExpKind kind)
{
    switch (kind) {
    case LOrExpKind::LAND:
        return "LAND";
    case LOrExpKind::LORANDLAND:
        return "LORANDLAND";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view land_exp_kind_to_cstr(LAndExpKind kind)
{
    switch (kind) {
    case LAndExpKind::EQ:
        return "EQ";
    case LAndExpKind::LANDANDEQ:
        return "LANDANDEQ";
    default:
        return "UNKNOWN";
    }
}

inline std::string_view eq_exp_kind_to_cstr(EqExpKind kind)
{
    switch (kind) {
    case EqExpKind::REL:
        return "REL";
    case EqExpKind::EQANDREL:
        return "EQANDREL";
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
    std::unique_ptr<BaseAST> lor_exp;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "ExpAST {\n";
        lor_exp->Dump(indent + 2);
        std::cout << "\n";
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

class AddExpAST : public BaseAST {
public:
    AddExpKind epk;
    std::unique_ptr<BaseAST> add_exp;
    std::unique_ptr<BaseAST> mul_exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "AddExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::add_exp_kind_to_cstr(epk) << "\n";

        if (epk == AddExpKind::ADDANDMUL) {
            std::cout << ast_dump_detail::indent(indent + 2) << "op: " << op << "\n";
            if (add_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "lhs_add_exp:\n";
                add_exp->Dump(indent + 4);
                std::cout << "\n";
            }
            if (mul_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rhs_mul_exp:\n";
                mul_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        } else {
            if (mul_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "mul_exp:\n";
                mul_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        }

        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class MulExpAST : public BaseAST {
public:
    MulExpKind epk;
    std::unique_ptr<BaseAST> unary_exp;
    std::unique_ptr<BaseAST> mul_exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "MulExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::mul_exp_kind_to_cstr(epk) << "\n";

        if (epk == MulExpKind::MULANDUNARY) {
            std::cout << ast_dump_detail::indent(indent + 2) << "op: " << op << "\n";
            if (mul_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "lhs_mul_exp:\n";
                mul_exp->Dump(indent + 4);
                std::cout << "\n";
            }
            if (unary_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rhs_unary_exp:\n";
                unary_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        } else {
            if (unary_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "unary_exp:\n";
                unary_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        }

        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class RelExpAST : public BaseAST {
public:
    RelExpKind epk;
    std::unique_ptr<BaseAST> add_exp;
    std::unique_ptr<BaseAST> rel_exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "RelExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::rel_exp_kind_to_cstr(epk) << "\n";

        if (epk == RelExpKind::RELANDADD) {
            std::cout << ast_dump_detail::indent(indent + 2) << "op: " << op << "\n";
            if (rel_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "lhs_rel_exp:\n";
                rel_exp->Dump(indent + 4);
                std::cout << "\n";
            }
            if (add_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rhs_add_exp:\n";
                add_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        } else {
            if (add_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "add_exp:\n";
                add_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        }

        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class LOrExpAST : public BaseAST {
public:
    LOrExpKind epk;
    std::unique_ptr<BaseAST> land_exp;
    std::unique_ptr<BaseAST> lor_exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "LOrExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::lor_exp_kind_to_cstr(epk) << "\n";

        if (epk == LOrExpKind::LORANDLAND) {
            std::cout << ast_dump_detail::indent(indent + 2) << "op: " << op << "\n";
            if (lor_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "lhs_lor_exp:\n";
                lor_exp->Dump(indent + 4);
                std::cout << "\n";
            }
            if (land_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rhs_land_exp:\n";
                land_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        } else {
            if (land_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "land_exp:\n";
                land_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        }

        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class LAndExpAST : public BaseAST {
public:
    LAndExpKind epk;
    std::unique_ptr<BaseAST> land_exp;
    std::unique_ptr<BaseAST> eq_exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "LAndExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::land_exp_kind_to_cstr(epk) << "\n";

        if (epk == LAndExpKind::LANDANDEQ) {
            std::cout << ast_dump_detail::indent(indent + 2) << "op: " << op << "\n";
            if (land_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "lhs_land_exp:\n";
                land_exp->Dump(indent + 4);
                std::cout << "\n";
            }
            if (eq_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rhs_eq_exp:\n";
                eq_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        } else {
            if (eq_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "eq_exp:\n";
                eq_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        }

        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};

class EqExpAST : public BaseAST {
public:
    EqExpKind epk;
    std::unique_ptr<BaseAST> rel_exp;
    std::unique_ptr<BaseAST> eq_exp;
    std::string op;

    void Dump(int indent = 0) const override
    {
        std::cout << ast_dump_detail::indent(indent) << "EqExpAST {\n";
        std::cout << ast_dump_detail::indent(indent + 2) << "kind: "
                  << ast_dump_detail::eq_exp_kind_to_cstr(epk) << "\n";

        if (epk == EqExpKind::EQANDREL) {
            std::cout << ast_dump_detail::indent(indent + 2) << "op: " << op << "\n";
            if (eq_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "lhs_eq_exp:\n";
                eq_exp->Dump(indent + 4);
                std::cout << "\n";
            }
            if (rel_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rhs_rel_exp:\n";
                rel_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        } else {
            if (rel_exp) {
                std::cout << ast_dump_detail::indent(indent + 2) << "rel_exp:\n";
                rel_exp->Dump(indent + 4);
                std::cout << "\n";
            }
        }

        std::cout << ast_dump_detail::indent(indent) << "}";
    }
};
