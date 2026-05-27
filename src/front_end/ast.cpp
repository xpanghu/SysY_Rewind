#include "ast.h"

namespace ast_dump_detail
{

IndentToken indent(int n)
{
    return {n};
}

std::ostream& operator<<(std::ostream& out, IndentToken token)
{
    for (int i = 0; i < token.n; i++) {
        out.put(' ');
    }
    return out;
}

std::string_view unary_op_to_cstr(UnaryOp op)
{
    switch (op) {
    case UnaryOp::PLUS:
        return "+";
    case UnaryOp::MINUS:
        return "-";
    case UnaryOp::NOT:
        return "!";
    default:
        return "UNKNOWN";
    }
}

std::string_view binary_op_to_cstr(BinaryOp op)
{
    switch (op) {
    case BinaryOp::MUL:
        return "*";
    case BinaryOp::DIV:
        return "/";
    case BinaryOp::MOD:
        return "%";
    case BinaryOp::ADD:
        return "+";
    case BinaryOp::SUB:
        return "-";
    case BinaryOp::EQ:
        return "==";
    case BinaryOp::NEQ:
        return "!=";
    case BinaryOp::LT:
        return "<";
    case BinaryOp::GT:
        return ">";
    case BinaryOp::LE:
        return "<=";
    case BinaryOp::GE:
        return ">=";
    case BinaryOp::LAND:
        return "&&";
    case BinaryOp::LOR:
        return "||";
    default:
        return "UNKNOWN";
    }
}

std::string_view btype_to_cstr(BType type)
{
    switch (type) {
    case BType::INT:
        return "int";
    default:
        return "UNKNOWN";
    }
}

std::string_view func_type_to_cstr(FuncType type)
{
    switch (type) {
    case FuncType::INT:
        return "int";
    case FuncType::VOID:
        return "void";
    }
}

} // namespace ast_dump_detail

void CompUnitAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "CompUnitAST {\n";
    for (const auto& item : items) {
        item->Dump(out, indent + 2);
        out << "\n";
    }
    out << ast_dump_detail::indent(indent) << "}";
}

void FuncDefAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "FuncDefAST {\n";

    out << ast_dump_detail::indent(indent + 2) << "func_type:\n";
    out << ast_dump_detail::indent(indent + 2)
        << ast_dump_detail::func_type_to_cstr(func_type) << "\n";

    out << ast_dump_detail::indent(indent + 2) << "ident: " << ident << "\n";
    if (block) {
        out << ast_dump_detail::indent(indent + 2) << "block:\n";
        block->Dump(out, indent + 4);
        out << "\n";
    }
    out << ast_dump_detail::indent(indent) << "}";
}

void FuncFParamAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "FuncFParamAST {\n";
    out << ast_dump_detail::indent(indent + 2) << "type: "
        << ast_dump_detail::btype_to_cstr(type) << "\n";
    out << ast_dump_detail::indent(indent + 2) << "ident: " << ident << "\n";
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Scalar>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: scalar\n";
        } else if constexpr (std::is_same_v<T, Array>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: array\n";
            if (!value.array_dim_size.empty()) {
                out << ast_dump_detail::indent(indent + 2) << "dims:\n";
                out << ast_dump_detail::indent(indent + 4) << "[]\n";
                for (const auto& dim : value.array_dim_size) {
                    if (dim) {
                        dim->Dump(out, indent + 4);
                        out << "\n";
                    }
                }
            } else {
                out << ast_dump_detail::indent(indent + 2) << "dims:\n";
                out << ast_dump_detail::indent(indent + 4) << "[]\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void FuncRParamsAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "FuncRParamsAST {\n";
    out << ast_dump_detail::indent(indent + 2) << "args: [\n";
    for (const auto& exp : exps) {
        if (exp) {
            exp->Dump(out, indent + 4);
            out << "\n";
        }
    }
    out << ast_dump_detail::indent(indent + 2) << "]\n";
    out << ast_dump_detail::indent(indent) << "}";
}

void DeclAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "DeclAST {\n";
    if (const_or_var) {
        const_or_var->Dump(out, indent + 2);
        out << "\n";
    }
    out << ast_dump_detail::indent(indent) << "}";
}

void ConstDeclAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "ConstDeclAST {\n";
    out << ast_dump_detail::indent(indent + 2) << "type: "
        << ast_dump_detail::btype_to_cstr(type) << "\n";
    out << ast_dump_detail::indent(indent + 2) << "const_defs: [\n";
    for (const auto& def : const_defs) {
        if (def) {
            def->Dump(out, indent + 4);
            out << "\n";
        }
    }
    out << ast_dump_detail::indent(indent + 2) << "]\n";
    out << ast_dump_detail::indent(indent) << "}";
}

void ConstDefAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "ConstDefAST {\n";
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ConstExpr>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: scalar\n";
            out << ast_dump_detail::indent(indent + 2) << "ident: " << value.ident << "\n";
            if (value.const_init_val) {
                out << ast_dump_detail::indent(indent + 2) << "init_val:\n";
                value.const_init_val->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, ConstArray>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: array\n";
            out << ast_dump_detail::indent(indent + 2) << "ident: " << value.ident << "[";
            for (size_t i = 0; i < value.const_dims.size(); ++i) {
                if (i > 0) out << "][";
                if (value.const_dims[i]) {
                    value.const_dims[i]->Dump(out, 0);
                }
                out << "]";
            }
            out << "\n";
            if (value.const_init_val) {
                out << ast_dump_detail::indent(indent + 2) << "init_val:\n";
                value.const_init_val->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void ConstInitValAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "ConstInitValAST {\n";
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ConstExprInit>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: scalar\n";
            if (value.const_exp) {
                value.const_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, ConstArrayInit>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: array\n";
            for (const auto& exp_item : value.const_inits) {
                if (exp_item) {
                    exp_item->Dump(out, indent + 4);
                    out << "\n";
                }
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void InitValAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "InitValAST {\n";
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ExprInit>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: scalar\n";
            if (value.exp) {
                value.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, ArrayInit>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: array\n";
            for (const auto& init_item : value.inits) {
                if (init_item) {
                    init_item->Dump(out, indent + 4);
                    out << "\n";
                }
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void VarDeclAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "VarDeclAST {\n";
    out << ast_dump_detail::indent(indent + 2) << "type: " << ast_dump_detail::btype_to_cstr(type) << "\n";
    out << ast_dump_detail::indent(indent + 2) << "var_defs: [\n";
    for (const auto& def : var_defs) {
        if (def) {
            def->Dump(out, indent + 4);
            out << "\n";
        }
    }
    out << ast_dump_detail::indent(indent + 2) << "]\n";
    out << ast_dump_detail::indent(indent) << "}";
}

void VarDefAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "VarDefAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, UninitializedScalar>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: scalar\n";
            out << ast_dump_detail::indent(indent + 2) << "ident: " << v.ident << "\n";
        } else if constexpr (std::is_same_v<T, InitializedScalar>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: scalar\n";
            out << ast_dump_detail::indent(indent + 2) << "ident: " << v.ident << "\n";
            if (v.init_val) {
                out << ast_dump_detail::indent(indent + 2) << "init_val:\n";
                v.init_val->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, UninitializedArray>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: array\n";
            out << ast_dump_detail::indent(indent + 2) << "ident: " << v.ident << "[";
            for (size_t i = 0; i < v.const_dims.size(); ++i) {
                if (i > 0) out << "][";
                if (v.const_dims[i]) {
                    v.const_dims[i]->Dump(out, 0);
                }
                out << "]";
            }
            out << "\n";
        } else if constexpr (std::is_same_v<T, InitializedArray>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: array\n";
            out << ast_dump_detail::indent(indent + 2) << "ident: " << v.ident << "[";
            for (size_t i = 0; i < v.const_dims.size(); ++i) {
                if (i > 0) out << "][";
                if (v.const_dims[i]) {
                    v.const_dims[i]->Dump(out, 0);
                }
                out << "]";
            }
            out << "\n";
            if (v.init_val) {
                out << ast_dump_detail::indent(indent + 2) << "init_val:\n";
                v.init_val->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void BlockAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "BlockAST {\n";
    for (const auto& item : items) {
        if (item) {
            item->Dump(out, indent + 2);
            out << "\n";
        }
    }
    out << ast_dump_detail::indent(indent) << "}";
}

void StmtAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "StmtAST {\n";
    std::visit([&](const auto& stmt) {
        using T = std::decay_t<decltype(stmt)>;
        if constexpr (std::is_same_v<T, Return>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: return\n";
            if (stmt.exp) {
                out << ast_dump_detail::indent(indent + 2) << "exp:\n";
                stmt.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Assign>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: assign\n";
            if (stmt.lval) {
                out << ast_dump_detail::indent(indent + 2) << "lval:\n";
                stmt.lval->Dump(out, indent + 4);
                out << "\n";
            }
            if (stmt.exp) {
                out << ast_dump_detail::indent(indent + 2) << "exp:\n";
                stmt.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Block>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: block\n";
            if (stmt.block) {
                out << ast_dump_detail::indent(indent + 2) << "block:\n";
                stmt.block->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Exp>) {
            out << ast_dump_detail::indent(indent + 2)
                << "kind: " << (stmt.exp ? "exp" : "empty") << "\n";
            if (stmt.exp) {
                out << ast_dump_detail::indent(indent + 2) << "exp:\n";
                stmt.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, SelectStmt>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: if\n";
            if (stmt.exp) {
                out << ast_dump_detail::indent(indent + 2) << "cond:\n";
                stmt.exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (stmt.then_stmt) {
                out << ast_dump_detail::indent(indent + 2) << "then:\n";
                stmt.then_stmt->Dump(out, indent + 4);
                out << "\n";
            }
            if (stmt.else_stmt) {
                out << ast_dump_detail::indent(indent + 2) << "else:\n";
                stmt.else_stmt->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, LoopStmt>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: while\n";
            if (stmt.exp) {
                out << ast_dump_detail::indent(indent + 2) << "cond:\n";
                stmt.exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (stmt.body_stmt) {
                out << ast_dump_detail::indent(indent + 2) << "body:\n";
                stmt.body_stmt->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, LoopControlStmt>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: "
                << (stmt.kind == StmtAST::LoopControlStmt::Kind::Break ? "break" : "continue")
                << "\n";
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void ExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "ExpAST {\n";
    if (lor_exp) {
        lor_exp->Dump(out, indent + 2);
        out << "\n";
    }
    out << ast_dump_detail::indent(indent) << "}";
}

void LOrExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "LOrExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Simple>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: land\n";
            if (v.land_exp) {
                v.land_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Binary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: lor\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::binary_op_to_cstr(v.op) << "\n";
            if (v.lor_exp) {
                out << ast_dump_detail::indent(indent + 2) << "lhs:\n";
                v.lor_exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (v.land_exp) {
                out << ast_dump_detail::indent(indent + 2) << "rhs:\n";
                v.land_exp->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void LAndExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "LAndExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Simple>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: eq\n";
            if (v.eq_exp) {
                v.eq_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Binary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: land\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::binary_op_to_cstr(v.op) << "\n";
            if (v.land_exp) {
                out << ast_dump_detail::indent(indent + 2) << "lhs:\n";
                v.land_exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (v.eq_exp) {
                out << ast_dump_detail::indent(indent + 2) << "rhs:\n";
                v.eq_exp->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void EqExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "EqExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Simple>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: rel\n";
            if (v.rel_exp) {
                v.rel_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Binary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: eq\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::binary_op_to_cstr(v.op) << "\n";
            if (v.eq_exp) {
                out << ast_dump_detail::indent(indent + 2) << "lhs:\n";
                v.eq_exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (v.rel_exp) {
                out << ast_dump_detail::indent(indent + 2) << "rhs:\n";
                v.rel_exp->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void RelExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "RelExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Simple>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: add\n";
            if (v.add_exp) {
                v.add_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Binary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: rel\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::binary_op_to_cstr(v.op) << "\n";
            if (v.rel_exp) {
                out << ast_dump_detail::indent(indent + 2) << "lhs:\n";
                v.rel_exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (v.add_exp) {
                out << ast_dump_detail::indent(indent + 2) << "rhs:\n";
                v.add_exp->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void AddExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "AddExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Simple>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: mul\n";
            if (v.mul_exp) {
                v.mul_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Binary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: add\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::binary_op_to_cstr(v.op) << "\n";
            if (v.add_exp) {
                out << ast_dump_detail::indent(indent + 2) << "lhs:\n";
                v.add_exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (v.mul_exp) {
                out << ast_dump_detail::indent(indent + 2) << "rhs:\n";
                v.mul_exp->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void MulExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "MulExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Simple>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: unary\n";
            if (v.unary_exp) {
                v.unary_exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Binary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: mul\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::binary_op_to_cstr(v.op) << "\n";
            if (v.mul_exp) {
                out << ast_dump_detail::indent(indent + 2) << "lhs:\n";
                v.mul_exp->Dump(out, indent + 4);
                out << "\n";
            }
            if (v.unary_exp) {
                out << ast_dump_detail::indent(indent + 2) << "rhs:\n";
                v.unary_exp->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void UnaryExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "UnaryExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Primary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: primary\n";
            if (v.exp) {
                v.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, Unary>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: unary\n";
            out << ast_dump_detail::indent(indent + 2) << "op: "
                << ast_dump_detail::unary_op_to_cstr(v.op) << "\n";
            if (v.exp) {
                v.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, FuncCall>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: call\n";
            out << ast_dump_detail::indent(indent + 2) << "callee: " << v.ident << "\n";
            if (v.func_r_params) {
                out << ast_dump_detail::indent(indent + 2) << "args:\n";
                v.func_r_params->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void PrimaryExpAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "PrimaryExpAST {\n";
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Number>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: number\n";
            out << ast_dump_detail::indent(indent + 2) << "value: " << v.value << "\n";
        } else if constexpr (std::is_same_v<T, Expression>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: expression\n";
            if (v.exp) {
                v.exp->Dump(out, indent + 4);
                out << "\n";
            }
        } else if constexpr (std::is_same_v<T, LValue>) {
            out << ast_dump_detail::indent(indent + 2) << "kind: lvalue\n";
            if (v.lval) {
                v.lval->Dump(out, indent + 4);
                out << "\n";
            }
        }
    },
               payload);
    out << ast_dump_detail::indent(indent) << "}";
}

void LValAST::Dump(std::ostream& out, int indent) const
{
    out << ast_dump_detail::indent(indent) << "LValAST {\n";
    out << "\n";
    out << ast_dump_detail::indent(indent) << "}";
}
