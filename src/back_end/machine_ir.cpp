#include "machine_ir.h"

namespace riscv
{

MachineOperand MachineOperand::phys_reg(Register reg)
{
    MachineOperand operand(MachineOperandKind::PhysReg);
    operand.reg_ = reg;
    return operand;
}

MachineOperand MachineOperand::virt_reg(uint32_t id)
{
    MachineOperand operand(MachineOperandKind::VirtReg);
    operand.virt_reg_ = id;
    return operand;
}

MachineOperand MachineOperand::imm(int32_t value)
{
    MachineOperand operand(MachineOperandKind::Imm);
    operand.imm_ = value;
    return operand;
}

MachineOperand MachineOperand::frame_index(int32_t index)
{
    MachineOperand operand(MachineOperandKind::FrameIndex);
    operand.frame_index_ = index;
    return operand;
}

MachineOperand MachineOperand::label(std::string value)
{
    MachineOperand operand(MachineOperandKind::Label);
    operand.label_ = std::move(value);
    return operand;
}

MachineInstr::MachineInstr(MachineOpcode opcode, std::vector<MachineOperand> operands) :
    opcode_(opcode),
    operands_(std::move(operands))
{
}

MachineInstr MachineInstr::make_label(std::string label)
{
    return MachineInstr(MachineOpcode::LABEL, {MachineOperand::label(std::move(label))});
}

MachineInstr MachineInstr::make_li(Register rd, int32_t imm)
{
    return MachineInstr(MachineOpcode::LI, {MachineOperand::phys_reg(rd), MachineOperand::imm(imm)});
}

MachineInstr MachineInstr::make_mv(Register rd, Register rs)
{
    return MachineInstr(MachineOpcode::MV, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs)});
}

MachineInstr MachineInstr::make_add(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::ADD, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_addi(Register rd, Register rs1, int32_t imm)
{
    return MachineInstr(MachineOpcode::ADDI, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::imm(imm)});
}

MachineInstr MachineInstr::make_sub(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::SUB, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_mul(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::MUL, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_div(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::DIV, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_rem(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::REM, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_and(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::AND, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_or(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::OR, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_xor(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::XOR, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_slt(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::SLT, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_sll(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::SLL, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_srl(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::SRL, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_sra(Register rd, Register rs1, Register rs2)
{
    return MachineInstr(MachineOpcode::SRA, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs1), MachineOperand::phys_reg(rs2)});
}

MachineInstr MachineInstr::make_seqz(Register rd, Register rs)
{
    return MachineInstr(MachineOpcode::SEQZ, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs)});
}

MachineInstr MachineInstr::make_snez(Register rd, Register rs)
{
    return MachineInstr(MachineOpcode::SNEZ, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(rs)});
}

MachineInstr MachineInstr::make_la(Register rd, std::string label)
{
    return MachineInstr(MachineOpcode::LA, {MachineOperand::phys_reg(rd), MachineOperand::label(std::move(label))});
}

MachineInstr MachineInstr::make_lw(Register rd, Register base, int32_t offset)
{
    return MachineInstr(MachineOpcode::LW, {MachineOperand::phys_reg(rd), MachineOperand::phys_reg(base), MachineOperand::imm(offset)});
}

MachineInstr MachineInstr::make_sw(Register rs, Register base, int32_t offset)
{
    return MachineInstr(MachineOpcode::SW, {MachineOperand::phys_reg(rs), MachineOperand::phys_reg(base), MachineOperand::imm(offset)});
}

MachineInstr MachineInstr::make_call(std::string label)
{
    return MachineInstr(MachineOpcode::CALL, {MachineOperand::label(std::move(label))});
}

MachineInstr MachineInstr::make_bnez(Register rs, std::string label)
{
    return MachineInstr(MachineOpcode::BNEZ, {MachineOperand::phys_reg(rs), MachineOperand::label(std::move(label))});
}

MachineInstr MachineInstr::make_j(std::string label)
{
    return MachineInstr(MachineOpcode::J, {MachineOperand::label(std::move(label))});
}

MachineInstr MachineInstr::make_ret()
{
    return MachineInstr(MachineOpcode::RET);
}

bool MachineInstr::is_terminator() const
{
    return opcode_ == MachineOpcode::J || opcode_ == MachineOpcode::RET;
}

MachineBasicBlock::MachineBasicBlock(std::string label) : label_(std::move(label))
{
}

void MachineBasicBlock::add_instr(MachineInstr instr)
{
    instrs_.push_back(std::move(instr));
}

MachineFunction::MachineFunction(std::string name) : name_(std::move(name))
{
}

MachineBasicBlock& MachineFunction::add_block(std::string label)
{
    blocks_.emplace_back(std::move(label));
    return blocks_.back();
}

} // namespace riscv
