#include "riscv.h"

#include <deque>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace riscv {
namespace {

    class RawProgramEmitter {
    public:
        explicit RawProgramEmitter(std::ostream& out)
            : out_(out)
        {
            reset_register_state();
        }

        void emit_program(const koopa_raw_program_t& program)
        {
            out_ << "  .text\n";
            // 输出program中全部function name
            for (size_t i = 0; i < program.funcs.len; ++i) {
                auto func = reinterpret_cast<koopa_raw_function_t>(program.funcs.buffer[i]);
                out_ << "  .globl " << skip_first(func->name) << "\n";
            }
            visit_slice(program.funcs);
        }

    private:
        enum class Register {
            x0,
            t0,
            t1,
            t2,
            t3,
            t4,
            t5,
            t6,
            a0,
        };

        static std::string_view skip_first(std::string_view sv)
        {
            return sv.empty() ? sv : sv.substr(1);
        }

        static const char* reg_name(Register reg)
        {
            switch (reg) {
            case Register::x0:
                return "x0";
            case Register::t0:
                return "t0";
            case Register::t1:
                return "t1";
            case Register::t2:
                return "t2";
            case Register::t3:
                return "t3";
            case Register::t4:
                return "t4";
            case Register::t5:
                return "t5";
            case Register::t6:
                return "t6";
            case Register::a0:
                return "a0";
            }
            throw std::runtime_error("unknown register");
        }

        void reset_register_state()
        {
            free_regs_.clear();
            free_regs_.push_back(Register::t0);
            free_regs_.push_back(Register::t1);
            free_regs_.push_back(Register::t2);
            free_regs_.push_back(Register::t3);
            free_regs_.push_back(Register::t4);
            free_regs_.push_back(Register::t5);
            free_regs_.push_back(Register::t6);
            reg_map_.clear();
        }

        void visit_slice(const koopa_raw_slice_t& slice)
        {
            for (size_t i = 0; i < slice.len; ++i) {
                auto ptr = slice.buffer[i];
                switch (slice.kind) {
                case KOOPA_RSIK_FUNCTION:
                    visit_function(reinterpret_cast<koopa_raw_function_t>(ptr));
                    break;
                case KOOPA_RSIK_BASIC_BLOCK:
                    visit_basic_block(reinterpret_cast<koopa_raw_basic_block_t>(ptr));
                    break;
                case KOOPA_RSIK_VALUE:
                    visit_value(reinterpret_cast<koopa_raw_value_t>(ptr));
                    break;
                default:
                    throw std::runtime_error("unsupported koopa_raw_slice_item_kind in riscv emitter");
                }
            }
        }

        void visit_function(const koopa_raw_function_t& func)
        {
            reset_register_state();
            out_ << skip_first(func->name) << ":\n";
            visit_slice(func->bbs);
        }

        void visit_basic_block(const koopa_raw_basic_block_t& bb)
        {
            visit_slice(bb->insts);
        }

        void visit_value(const koopa_raw_value_t& value)
        {
            const auto& kind = value->kind;
            switch (kind.tag) {
            case KOOPA_RVT_RETURN:
                visit_return(kind.data.ret);
                break;
            case KOOPA_RVT_INTEGER:
                break;
            case KOOPA_RVT_BINARY:
                visit_binary(value);
                break;
            default:
                throw std::runtime_error("unsupported koopa value kind in riscv emitter");
            }
        }

        Register require_assigned_register(koopa_raw_value_t value)
        {
            auto it = reg_map_.find(value);
            if (it == reg_map_.end()) {
                throw std::runtime_error("value register not assigned");
            }
            return it->second;
        }

        Register assign_register_for_value(koopa_raw_value_t value)
        {
            auto it = reg_map_.find(value);
            if (it != reg_map_.end()) {
                return it->second;
            }
            if (free_regs_.empty()) {
                throw std::runtime_error("temporary register exhausted");
            }
            Register reg = free_regs_.front();
            free_regs_.pop_front();
            reg_map_[value] = reg;
            return reg;
        }

        Register materialize_operand(koopa_raw_value_t value, Register preferred)
        {
            const auto& kind = value->kind;
            if (kind.tag == KOOPA_RVT_INTEGER) {
                if (kind.data.integer.value == 0) {
                    return Register::x0;
                }
                emit_li(preferred, std::to_string(kind.data.integer.value));
                return preferred;
            }
            return require_assigned_register(value);
        }

        Register materialize_operand(koopa_raw_value_t value)
        {
            const auto& kind = value->kind;
            if (kind.tag == KOOPA_RVT_INTEGER) {
                if (kind.data.integer.value == 0) {
                    return Register::x0;
                }
                if (free_regs_.empty()) {
                    throw std::runtime_error("temporary register exhausted");
                }
                Register reg = free_regs_.front();
                free_regs_.pop_front();
                emit_li(reg, std::to_string(kind.data.integer.value));
                return reg;
            }
            return require_assigned_register(value);
        }

        void visit_binary(const koopa_raw_value_t& value)
        {
            const auto& binary = value->kind.data.binary;
            Register dst = assign_register_for_value(value);

            switch (binary.op) {
            case KOOPA_RBO_EQ: {
                Register lhs = materialize_operand(binary.lhs, dst);
                Register rhs = materialize_operand(binary.rhs);
                emit_xor(dst, lhs, rhs);
                emit_seqz(dst, dst);
                break;
            }
            case KOOPA_RBO_SUB: {
                Register lhs = materialize_operand(binary.lhs);
                Register rhs = materialize_operand(binary.rhs);
                emit_sub(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_ADD: {
                Register lhs = materialize_operand(binary.lhs);
                Register rhs = materialize_operand(binary.rhs);
                emit_add(dst, lhs, rhs);
                break;
            }
            default:
                throw std::runtime_error("unsupported koopa binary op in riscv emitter");
            }
        }

        void visit_return(const koopa_raw_return_t& ret)
        {
            if (ret.value != nullptr) {
                const auto& ret_kind = ret.value->kind;
                if (ret_kind.tag == KOOPA_RVT_INTEGER) {
                    emit_li(Register::a0, std::to_string(ret_kind.data.integer.value));
                } else {
                    Register src = require_assigned_register(ret.value);
                    emit_mv(Register::a0, src);
                }
            }
            out_ << "  ret\n";
        }

        void emit_li(Register rd, const std::string& imm)
        {
            out_ << "  li    " << reg_name(rd) << ", " << imm << "\n";
        }

        void emit_xor(Register rd, Register rs1, Register rs2)
        {
            out_ << "  xor   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_seqz(Register rd, Register rs)
        {
            out_ << "  seqz  " << reg_name(rd) << ", " << reg_name(rs) << "\n";
        }

        void emit_sub(Register rd, Register rs1, Register rs2)
        {
            out_ << "  sub   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_add(Register rd, Register rs1, Register rs2)
        {
            out_ << "  add   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_mv(Register rd, Register rs)
        {
            out_ << "  mv    " << reg_name(rd) << ", " << reg_name(rs) << "\n";
        }

        std::ostream& out_;
        std::deque<Register> free_regs_;
        std::unordered_map<koopa_raw_value_t, Register> reg_map_;
    };

} // namespace

void emit_program(const koopa_raw_program_t& program, std::ostream& out)
{
    RawProgramEmitter emitter(out);
    emitter.emit_program(program);
}

} // namespace riscv
