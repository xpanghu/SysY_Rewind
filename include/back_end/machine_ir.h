#pragma once

#include "asm_writer.h"
#include "machine_frame.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace riscv
{

enum class MachineOperandKind {
    PhysReg,
    VirtReg,
    Imm,
    FrameIndex,
    Label,
};

class MachineOperand
{
public:
    static MachineOperand phys_reg(Register reg);
    static MachineOperand virt_reg(uint32_t id);
    static MachineOperand imm(int32_t value);
    static MachineOperand frame_index(int32_t index);
    static MachineOperand label(std::string value);

    MachineOperandKind kind() const
    {
        return kind_;
    }

    Register reg() const
    {
        return reg_;
    }

    int32_t imm() const
    {
        return imm_;
    }

    uint32_t virt_reg() const
    {
        return virt_reg_;
    }

    int32_t frame_index() const
    {
        return frame_index_;
    }

    const std::string& label() const
    {
        return label_;
    }

private:
    explicit MachineOperand(MachineOperandKind kind) : kind_(kind)
    {
    }

    MachineOperandKind kind_;
    Register reg_ = Register::x0;
    uint32_t virt_reg_ = 0;
    int32_t imm_ = 0;
    int32_t frame_index_ = 0;
    std::string label_;
};

enum class MachineOpcode {
    LABEL,
    LI,
    MV,
    ADD,
    ADDI,
    SUB,
    MUL,
    DIV,
    REM,
    AND,
    OR,
    XOR,
    SLT,
    SLL,
    SRL,
    SRA,
    SEQZ,
    SNEZ,
    LA,
    LW,
    SW,
    CALL,
    BNEZ,
    J,
    RET,
};

class MachineInstr
{
public:
    explicit MachineInstr(MachineOpcode opcode, std::vector<MachineOperand> operands = {});

    static MachineInstr make_li(Register rd, int32_t imm);
    static MachineInstr make_label(std::string label);
    static MachineInstr make_mv(Register rd, Register rs);
    static MachineInstr make_add(Register rd, Register rs1, Register rs2);
    static MachineInstr make_addi(Register rd, Register rs1, int32_t imm);
    static MachineInstr make_sub(Register rd, Register rs1, Register rs2);
    static MachineInstr make_mul(Register rd, Register rs1, Register rs2);
    static MachineInstr make_div(Register rd, Register rs1, Register rs2);
    static MachineInstr make_rem(Register rd, Register rs1, Register rs2);
    static MachineInstr make_and(Register rd, Register rs1, Register rs2);
    static MachineInstr make_or(Register rd, Register rs1, Register rs2);
    static MachineInstr make_xor(Register rd, Register rs1, Register rs2);
    static MachineInstr make_slt(Register rd, Register rs1, Register rs2);
    static MachineInstr make_sll(Register rd, Register rs1, Register rs2);
    static MachineInstr make_srl(Register rd, Register rs1, Register rs2);
    static MachineInstr make_sra(Register rd, Register rs1, Register rs2);
    static MachineInstr make_seqz(Register rd, Register rs);
    static MachineInstr make_snez(Register rd, Register rs);
    static MachineInstr make_la(Register rd, std::string label);
    static MachineInstr make_lw(Register rd, Register base, int32_t offset);
    static MachineInstr make_sw(Register rs, Register base, int32_t offset);
    static MachineInstr make_call(std::string label);
    static MachineInstr make_bnez(Register rs, std::string label);
    static MachineInstr make_j(std::string label);
    static MachineInstr make_ret();

    MachineOpcode opcode() const
    {
        return opcode_;
    }

    const std::vector<MachineOperand>& operands() const
    {
        return operands_;
    }

    bool is_terminator() const;

private:
    MachineOpcode opcode_;
    std::vector<MachineOperand> operands_;
};

class MachineBasicBlock
{
public:
    explicit MachineBasicBlock(std::string label);

    const std::string& label() const
    {
        return label_;
    }

    std::vector<MachineInstr>& instrs()
    {
        return instrs_;
    }

    const std::vector<MachineInstr>& instrs() const
    {
        return instrs_;
    }

    void add_instr(MachineInstr instr);

private:
    std::string label_;
    std::vector<MachineInstr> instrs_;
};

class MachineFunction
{
public:
    explicit MachineFunction(std::string name);

    const std::string& name() const
    {
        return name_;
    }

    MachineBasicBlock& add_block(std::string label);

    std::vector<MachineBasicBlock>& blocks()
    {
        return blocks_;
    }

    const std::vector<MachineBasicBlock>& blocks() const
    {
        return blocks_;
    }

    MachineFrame frame;

private:
    std::string name_;
    std::vector<MachineBasicBlock> blocks_;
};

} // namespace riscv
