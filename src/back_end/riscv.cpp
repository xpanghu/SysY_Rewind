#include "back_end/riscv.h"

#include <array>
#include <deque>
#include <iostream>
#include <optional>
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
            // 输出 program 中 func, 之后开始遍历 func
            for (size_t i = 0; i < program.funcs.len; ++i) {
                auto func = reinterpret_cast<koopa_raw_function_t>(program.funcs.buffer[i]);
                out_ << "  .globl " << skip_first(func->name) << "\n";
            }
            visit_slice(program.funcs);
        }

    private:
        enum class Register {
            x0, // 恒为0
            t0,
            t1,
            t2,
            t3,
            t4,
            t5,
            t6,
            a0,
            a1, // 函数参数/返回值
            a2,
            a3,
            a4,
            a5,
            a6,
            a7,
            kCount
        };

        // c++17 static constexpr可以在类中初始化
        static constexpr std::array<const char*, static_cast<size_t>(Register::kCount)> kRegisterNames_ {
            "x0",
            "t0",
            "t1",
            "t2",
            "t3",
            "t4",
            "t5",
            "t6",
            "a0",
            "a1",
            "a2",
            "a3",
            "a4",
            "a5",
            "a6",
            "a7",
        };

        static constexpr const char* reg_name(Register reg)
        {
            const auto idx = static_cast<size_t>(reg);
            assert(idx < kRegisterNames_.size());
            return kRegisterNames_[idx];
        }

        void reset_register_state()
        {
            // free_regs_.clear();
            free_regs_ = {
                Register::t0,
                Register::t1,
                Register::t2,
                Register::t3,
                Register::t4,
                Register::t5,
                Register::t6,
                Register::a2,
                Register::a3,
                Register::a4,
                Register::a5,
                Register::a6,
                Register::a7
            };
            reg_map_.clear();
        }

        static std::string_view skip_first(std::string_view sv)
        {
            return sv.empty() ? sv : sv.substr(1);
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
            case KOOPA_RVT_BINARY:
                visit_binary(value);
                break;
            default:
                break;
            }
        }

        // 返回已经被分配的寄存器
        Register require_assigned_register(koopa_raw_value_t value)
        {
            auto it = reg_map_.find(value);
            assert(it != reg_map_.end());
            return it->second;
        }

        // 分配新的寄存器
        Register assign_register_for_value(koopa_raw_value_t value)
        {
            // 判断寄存器数目是否为零
            assert(!free_regs_.empty());
            Register reg = free_regs_.front();
            free_regs_.pop_front();
            reg_map_[value] = reg;
            return reg;
        }

        // 用于二元指令操作数分配
        // 按 lhs->rhs 顺序检查并分配操作数寄存器。
        // dst 复用本轮最后新分配的寄存器；若本轮无新分配则分配新寄存器。
        Register ensure_operand_register(koopa_raw_value_t value, std::optional<Register>& last_new_reg)
        {
            const auto& kind = value->kind;
            if (kind.tag == KOOPA_RVT_INTEGER) {
                // 操作数为0, 返回特定寄存器 x0
                if (kind.data.integer.value == 0) {
                    return Register::x0;
                }
                Register reg = assign_register_for_value(value);
                emit_li(reg, std::to_string(kind.data.integer.value));
                last_new_reg = reg;
                return reg;
            }

            return require_assigned_register(value);
        }

        void visit_binary(const koopa_raw_value_t& value)
        {
            const auto& binary = value->kind.data.binary;
            std::optional<Register> last_new_reg;

            Register lhs = ensure_operand_register(binary.lhs, last_new_reg);
            Register rhs = ensure_operand_register(binary.rhs, last_new_reg);

            Register dst;
            if (last_new_reg.has_value()) {
                dst = *last_new_reg;
            } else {
                dst = assign_register_for_value(value);
            }

            reg_map_[value] = dst;

            switch (binary.op) {
            case KOOPA_RBO_EQ: {
                // lhs / rhs == 0 , 则只需要seqz一条指令
                emit_xor(dst, lhs, rhs);
                emit_seqz(dst, dst);
                break;
            }
            case KOOPA_RBO_NOT_EQ: {
                // lhs / rhs == 0 , 则只需要snez一条指令
                emit_xor(dst, lhs, rhs);
                emit_snez(dst, dst);
                break;
            }
            case KOOPA_RBO_SUB: {
                emit_sub(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_ADD: {
                emit_add(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_AND: {
                emit_and(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_OR: {
                emit_or(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_MUL: {
                emit_mul(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_DIV: {
                emit_div(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_MOD: {
                emit_rem(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_GT: {
                emit_gt(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_LT: {
                emit_lt(dst, lhs, rhs);
                break;
            }
            case KOOPA_RBO_GE: {
                // a >= b == !(a < b)
                emit_lt(dst, lhs, rhs);
                emit_seqz(dst, dst);
                break;
            }
            case KOOPA_RBO_LE: {
                // a <= b == !(a > b)
                emit_gt(dst, lhs, rhs);
                emit_seqz(dst, dst);
                break;
            }
            default:
                std::cout << "\n"
                          << binary.op << std::endl;
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

        void emit_gt(Register rd, Register rs1, Register rs2)
        {
            out_ << "  sgt   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_lt(Register rd, Register rs1, Register rs2)
        {
            out_ << "  slt   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_xor(Register rd, Register rs1, Register rs2)
        {
            out_ << "  xor   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_seqz(Register rd, Register rs)
        {
            out_ << "  seqz  " << reg_name(rd) << ", " << reg_name(rs) << "\n";
        }

        void emit_snez(Register rd, Register rs)
        {
            out_ << "  snez  " << reg_name(rd) << ", " << reg_name(rs) << "\n";
        }

        void emit_sub(Register rd, Register rs1, Register rs2)
        {
            out_ << "  sub   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_add(Register rd, Register rs1, Register rs2)
        {
            out_ << "  add   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_and(Register rd, Register rs1, Register rs2)
        {
            out_ << "  and   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_or(Register rd, Register rs1, Register rs2)
        {
            out_ << "  or    " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_mv(Register rd, Register rs)
        {
            out_ << "  mv    " << reg_name(rd) << ", " << reg_name(rs) << "\n";
        }

        void emit_mul(Register rd, Register rs1, Register rs2)
        {
            out_ << "  mul   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_div(Register rd, Register rs1, Register rs2)
        {
            out_ << "  div   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        void emit_rem(Register rd, Register rs1, Register rs2)
        {
            out_ << "  rem   " << reg_name(rd) << ", " << reg_name(rs1) << ", " << reg_name(rs2) << "\n";
        }

        std::ostream& out_; // 与hello.koopa绑定
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
