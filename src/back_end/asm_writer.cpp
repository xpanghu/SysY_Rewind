#include "asm_writer.h"

#include <ostream>
#include <stdexcept>

namespace riscv
{

AsmWriter::AsmWriter(std::ostream& out) : out_(out)
{
}

void AsmWriter::section_text()
{
    out_ << "  .text\n";
}

void AsmWriter::section_data()
{
    out_ << "  .data\n";
}

void AsmWriter::global(std::string_view symbol)
{
    out_ << "  .global " << symbol << "\n";
}

void AsmWriter::globl(std::string_view symbol)
{
    out_ << "  .globl " << symbol << "\n";
}

void AsmWriter::label(std::string_view symbol)
{
    out_ << symbol << ":\n";
}

void AsmWriter::blank_line()
{
    out_ << "\n";
}

void AsmWriter::word(int32_t value)
{
    out_ << "  .word " << value << "\n";
}

void AsmWriter::zero(int32_t size)
{
    out_ << "  .zero " << size << "\n";
}

void AsmWriter::li(Register rd, int32_t imm)
{
    out_ << "  li " << reg_name(rd) << ", " << imm << "\n";
}

void AsmWriter::mv(Register rd, Register rs)
{
    out_ << "  mv " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void AsmWriter::add(Register rd, Register rs1, Register rs2)
{
    out_ << "  add " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::addi(Register rd, Register rs1, int32_t imm)
{
    out_ << "  addi " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << imm << "\n";
}

void AsmWriter::sub(Register rd, Register rs1, Register rs2)
{
    out_ << "  sub " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::mul(Register rd, Register rs1, Register rs2)
{
    out_ << "  mul " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::div(Register rd, Register rs1, Register rs2)
{
    out_ << "  div " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::rem(Register rd, Register rs1, Register rs2)
{
    out_ << "  rem " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::and_(Register rd, Register rs1, Register rs2)
{
    out_ << "  and " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::or_(Register rd, Register rs1, Register rs2)
{
    out_ << "  or " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::xor_(Register rd, Register rs1, Register rs2)
{
    out_ << "  xor " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::slt(Register rd, Register rs1, Register rs2)
{
    out_ << "  slt " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::sll(Register rd, Register rs1, Register rs2)
{
    out_ << "  sll " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::srl(Register rd, Register rs1, Register rs2)
{
    out_ << "  srl " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::sra(Register rd, Register rs1, Register rs2)
{
    out_ << "  sra " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void AsmWriter::seqz(Register rd, Register rs)
{
    out_ << "  seqz " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void AsmWriter::snez(Register rd, Register rs)
{
    out_ << "  snez " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void AsmWriter::la(Register rd, std::string_view label)
{
    out_ << "  la " << reg_name(rd) << ", " << label << "\n";
}

void AsmWriter::lw(Register rd, Register rs1, int32_t offset)
{
    out_ << "  lw " << reg_name(rd) << ", " << offset
         << "(" << reg_name(rs1) << ")\n";
}

void AsmWriter::sw(Register rs2, Register rs1, int32_t offset)
{
    out_ << "  sw " << reg_name(rs2) << ", " << offset
         << "(" << reg_name(rs1) << ")\n";
}

void AsmWriter::ret()
{
    out_ << "  ret\n";
}

void AsmWriter::call_label(std::string_view label)
{
    out_ << "  call " << label << "\n";
}

void AsmWriter::bnez(Register rs, std::string_view label)
{
    out_ << "  bnez " << reg_name(rs) << ", " << label << "\n";
}

void AsmWriter::j(std::string_view label)
{
    out_ << "  j " << label << "\n";
}

const char* AsmWriter::reg_name(Register reg)
{
    switch (reg) {
    case Register::x0:
        return "x0";
    case Register::ra:
        return "ra";
    case Register::sp:
        return "sp";
    case Register::t0:
        return "t0";
    case Register::t1:
        return "t1";
    case Register::t2:
        return "t2";
    case Register::a0:
        return "a0";
    case Register::a1:
        return "a1";
    case Register::a2:
        return "a2";
    case Register::a3:
        return "a3";
    case Register::a4:
        return "a4";
    case Register::a5:
        return "a5";
    case Register::a6:
        return "a6";
    case Register::a7:
        return "a7";
    }
    throw std::runtime_error("unknown register");
}

} // namespace riscv
