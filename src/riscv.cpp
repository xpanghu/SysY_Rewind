#include "riscv.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace riscv {
namespace {

    std::string_view skip_first(std::string_view sv)
    {
        return sv.empty() ? sv : sv.substr(1);
    }

    void emit_insn(std::ostream& out, const std::string& op, const std::string& rd, const std::string& rs)
    {
        out << "  " << op << " " << rd << ", " << rs << "\n";
    }

    void visit_slice(const koopa_raw_slice_t& slice, std::ostream& out);
    void visit_function(const koopa_raw_function_t& func, std::ostream& out);
    void visit_basic_block(const koopa_raw_basic_block_t& bb, std::ostream& out);
    void visit_value(const koopa_raw_value_t& value, std::ostream& out);
    void visit_return(const koopa_raw_return_t& ret, std::ostream& out);

    void visit_slice(const koopa_raw_slice_t& slice, std::ostream& out)
    {
        for (size_t i = 0; i < slice.len; ++i) {
            auto ptr = slice.buffer[i];
            switch (slice.kind) {
            case KOOPA_RSIK_FUNCTION:
                visit_function(reinterpret_cast<koopa_raw_function_t>(ptr), out);
                break;
            case KOOPA_RSIK_BASIC_BLOCK:
                visit_basic_block(reinterpret_cast<koopa_raw_basic_block_t>(ptr), out);
                break;
            case KOOPA_RSIK_VALUE:
                visit_value(reinterpret_cast<koopa_raw_value_t>(ptr), out);
                break;
            default:
                throw std::runtime_error("unsupported koopa_raw_slice_item_kind in riscv emitter");
            }
        }
    }

    void visit_function(const koopa_raw_function_t& func, std::ostream& out)
    {
        out << skip_first(func->name) << ":\n";
        visit_slice(func->bbs, out);
    }

    void visit_basic_block(const koopa_raw_basic_block_t& bb, std::ostream& out)
    {
        visit_slice(bb->insts, out);
    }

    void visit_value(const koopa_raw_value_t& value, std::ostream& out)
    {
        const auto& kind = value->kind;
        switch (kind.tag) {
        case KOOPA_RVT_RETURN:
            visit_return(kind.data.ret, out);
            break;
        case KOOPA_RVT_INTEGER:
            // Integer constants are handled when referenced by instructions.
            break;
        default:
            throw std::runtime_error("unsupported koopa value kind in riscv emitter");
        }
    }

    void visit_return(const koopa_raw_return_t& ret, std::ostream& out)
    {
        if (ret.value != nullptr) {
            const auto& ret_kind = ret.value->kind;
            if (ret_kind.tag != KOOPA_RVT_INTEGER) {
                throw std::runtime_error("only integer return is supported in riscv emitter");
            }
            emit_insn(out, "li", "a0", std::to_string(ret_kind.data.integer.value));
        }
        out << "  ret\n";
    }

} // namespace

void emit_program(const koopa_raw_program_t& program, std::ostream& out)
{
    out << "  .text\n";
    for (size_t i = 0; i < program.funcs.len; ++i) {
        auto func = reinterpret_cast<koopa_raw_function_t>(program.funcs.buffer[i]);
        out << "  .global " << skip_first(func->name) << "\n";
    }
    visit_slice(program.funcs, out);
}

} // namespace riscv
