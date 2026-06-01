#include "machine_asm_printer.h"

#include "instruction_selector.h"
#include "machine_verifier.h"
#include <cctype>
#include <stdexcept>

namespace riscv
{

namespace
{

Register reg(const MachineOperand& operand)
{
    if (operand.kind() != MachineOperandKind::PhysReg) {
        throw std::runtime_error("expected physical register operand");
    }
    return operand.reg();
}

int32_t imm(const MachineOperand& operand)
{
    if (operand.kind() != MachineOperandKind::Imm) {
        throw std::runtime_error("expected immediate operand");
    }
    return operand.imm();
}

const std::string& label(const MachineOperand& operand)
{
    if (operand.kind() != MachineOperandKind::Label) {
        throw std::runtime_error("expected label operand");
    }
    return operand.label();
}

std::string sanitize_symbol(std::string_view name)
{
    if (!name.empty() && (name.front() == '@' || name.front() == '%')) {
        name.remove_prefix(1);
    }

    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '.') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }

    if (out.empty()) {
        out = "_anon";
    }
    return out;
}

} // namespace

MachineAsmPrinter::MachineAsmPrinter(std::ostream& out) : writer_(out)
{
}

void MachineAsmPrinter::emit_module(const rewind_ir::IRModule& module)
{
    for (const auto* global_value : module.global_values_) {
        emit_global_value(*global_value->as<rewind_ir::IRGlobalAllocInst>());
    }

    InstructionSelector selector;
    MachineVerifier verifier;
    for (const auto* func : module.funcs_) {
        if (func->is_declaration_) {
            continue;
        }

        MachineFunction machine_function = selector.select_function(*func);
        if (!verifier.verify(machine_function)) {
            throw std::runtime_error("MachineVerifier failed: " + verifier.report());
        }
        emit_function(machine_function);
    }
}

void MachineAsmPrinter::emit_global_value(const rewind_ir::IRGlobalAllocInst& global_alloc)
{
    const std::string name = sanitize_symbol(global_alloc.name_);
    if (global_alloc.type_ == nullptr || !global_alloc.type_->is_pointer()) {
        throw std::runtime_error("global alloc must have pointer type");
    }
    const auto* alloc_type = global_alloc.type_->as<rewind_ir::IRPointerType>();

    writer_.section_data();
    writer_.global(name);
    writer_.label(name);

    emit_global_initializer(global_alloc.init_, alloc_type->base_type);

    writer_.blank_line();
}

void MachineAsmPrinter::emit_global_initializer(const rewind_ir::IRValue* init,
                                                const rewind_ir::IRType* type)
{
    if (init->is_zero_init()) {
        const auto size = data_layout_.type_size(type);
        writer_.zero(size);
        return;
    }

    if (type->is_int32()) {
        const auto* constant = init->as<rewind_ir::IRConstant>();
        writer_.word(constant->value_);
        return;
    }

    if (type->is_array()) {
        const auto* array_type = type->as<rewind_ir::IRArrayType>();
        const auto* aggregate = init->as<rewind_ir::IRAggregate>();

        for (const auto* elem : aggregate->elems_) {
            emit_global_initializer(elem, array_type->element_type);
        }
        return;
    }

    throw std::runtime_error("unsupported global initializer type");
}

void MachineAsmPrinter::emit_function(const MachineFunction& function)
{
    writer_.section_text();
    writer_.globl(function.name());
    writer_.label(function.name());

    for (const auto& block : function.blocks()) {
        emit_basic_block(block);
    }

    writer_.blank_line();
}

void MachineAsmPrinter::emit_basic_block(const MachineBasicBlock& block)
{
    writer_.label(block.label());
    for (const auto& inst : block.instrs()) {
        emit_instruction(inst);
    }
}

void MachineAsmPrinter::emit_instruction(const MachineInstr& inst)
{
    const auto& operands = inst.operands();

    switch (inst.opcode()) {
    case MachineOpcode::LABEL:
        writer_.label(label(operands.at(0)));
        return;
    case MachineOpcode::LI:
        writer_.li(reg(operands.at(0)), imm(operands.at(1)));
        return;
    case MachineOpcode::MV:
        writer_.mv(reg(operands.at(0)), reg(operands.at(1)));
        return;
    case MachineOpcode::ADD:
        writer_.add(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::ADDI:
        writer_.addi(reg(operands.at(0)), reg(operands.at(1)), imm(operands.at(2)));
        return;
    case MachineOpcode::SUB:
        writer_.sub(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::MUL:
        writer_.mul(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::DIV:
        writer_.div(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::REM:
        writer_.rem(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::AND:
        writer_.and_(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::OR:
        writer_.or_(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::XOR:
        writer_.xor_(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::SLT:
        writer_.slt(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::SLL:
        writer_.sll(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::SRL:
        writer_.srl(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::SRA:
        writer_.sra(reg(operands.at(0)), reg(operands.at(1)), reg(operands.at(2)));
        return;
    case MachineOpcode::SEQZ:
        writer_.seqz(reg(operands.at(0)), reg(operands.at(1)));
        return;
    case MachineOpcode::SNEZ:
        writer_.snez(reg(operands.at(0)), reg(operands.at(1)));
        return;
    case MachineOpcode::LA:
        writer_.la(reg(operands.at(0)), label(operands.at(1)));
        return;
    case MachineOpcode::LW:
        writer_.lw(reg(operands.at(0)), reg(operands.at(1)), imm(operands.at(2)));
        return;
    case MachineOpcode::SW:
        writer_.sw(reg(operands.at(0)), reg(operands.at(1)), imm(operands.at(2)));
        return;
    case MachineOpcode::CALL:
        writer_.call_label(label(operands.at(0)));
        return;
    case MachineOpcode::BNEZ:
        writer_.bnez(reg(operands.at(0)), label(operands.at(1)));
        return;
    case MachineOpcode::J:
        writer_.j(label(operands.at(0)));
        return;
    case MachineOpcode::RET:
        writer_.ret();
        return;
    }

    throw std::runtime_error("unsupported machine opcode");
}

} // namespace riscv
