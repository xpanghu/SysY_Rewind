#pragma once

#include <cstdint>
#include <iosfwd>
#include <string_view>

namespace riscv
{

enum class Register {
    x0, // always zero
    ra, // save return address
    sp, // stack pointer
    t0, // t0 - t6 temporary register
    t1,
    t2,
    // a0 - a7 used to pass non-float arguments to a function during function calls
    // a0 a1 save return parameters
    a0,
    a1,
    a2,
    a3,
    a4,
    a5,
    a6,
    a7,
};

class AsmWriter
{
public:
    explicit AsmWriter(std::ostream& out);

    void section_text();
    void section_data();
    void global(std::string_view symbol);
    void globl(std::string_view symbol);
    void label(std::string_view symbol);
    void blank_line();

    void word(int32_t value);
    void zero(int32_t size);

    void li(Register rd, int32_t imm);
    void mv(Register rd, Register rs);
    void add(Register rd, Register rs1, Register rs2);
    void addi(Register rd, Register rs1, int32_t imm);
    void sub(Register rd, Register rs1, Register rs2);
    void mul(Register rd, Register rs1, Register rs2);
    void div(Register rd, Register rs1, Register rs2);
    void rem(Register rd, Register rs1, Register rs2);
    void and_(Register rd, Register rs1, Register rs2);
    void or_(Register rd, Register rs1, Register rs2);
    void xor_(Register rd, Register rs1, Register rs2);
    void slt(Register rd, Register rs1, Register rs2);
    void sll(Register rd, Register rs1, Register rs2);
    void srl(Register rd, Register rs1, Register rs2);
    void sra(Register rd, Register rs1, Register rs2);
    void seqz(Register rd, Register rs);
    void snez(Register rd, Register rs);
    void la(Register rd, std::string_view label);
    void lw(Register rd, Register rs1, int32_t offset);
    void sw(Register rs2, Register rs1, int32_t offset);
    void ret();
    void call_label(std::string_view label);
    void bnez(Register rs, std::string_view label);
    void j(std::string_view label);

    static const char* reg_name(Register reg);

private:
    std::ostream& out_;
};

} // namespace riscv
