#include <cstdint>
#include <iostream>
#include "riscv.h"
#include "rewind_ir.h"
#include <cctype>
#include <ostream>
#include <stdexcept>

namespace riscv
{

namespace
{

constexpr int32_t kWordSize = 4;

const int32_t* find_slot(const std::unordered_map<const rewind_ir::IRValue*, int32_t>& slots,
                         const rewind_ir::IRValue* value)
{
    const auto it = slots.find(value);
    if (it == slots.end()) {
        return nullptr;
    }
    return &it->second;
}

} // namespace

bool FunctionFrame::produces_stack_value(const rewind_ir::IRValue& value)
{
    switch (value.kind_) {
    case rewind_ir::IRValueKind::IR_BINARY:
    case rewind_ir::IRValueKind::IR_LOAD:
        return true;
    default:
        return false;
    }
}

int32_t FunctionFrame::alloc_size(const rewind_ir::IRAllocInst& inst)
{
    if (inst.type_ == nullptr || !inst.type_->is_pointer()) {
        return kWordSize;
    }

    const auto* pointer_type = inst.type_->as<rewind_ir::IRPointerType>();
    auto size = rewind_ir::IRTypeContext::instance().getTypeSize(pointer_type->base_type);
    return static_cast<int32_t>(size == 0 ? kWordSize : size);
}

int32_t FunctionFrame::align_to(int32_t value, int32_t align)
{
    return ((value + align - 1) / align) * align;
}

/*
 | previous function stack frame |     high address
 | ----------------------------- |
 |       saved registers         |
 |       local variables         |
 |       temp values             | <-- inst result
 |       function params         |     low address
 | ----------------------------- |
 |                               | <-- sp register address
 * the design of stack frame
 * 1. low address part stores function param slots
 * 2. then stores local variable object slots and IR median slots
 * 3. align 16 bytes
 * return address is currently stored in ra
 * improve: return address store in stack frame
 */

// ! return address store in ra register , not store in stack frame
void FunctionFrame::build(const rewind_ir::IRFunction& func)
{
    next_slot_offset_ = 0;
    frame_size_ = 16;
    // ra_offset_ = 12;
    object_slots_.clear();
    value_slots_.clear();

    for (const auto* block : func.basic_blocks_) {
        for (const auto* inst : block->insts_) {
            if (inst->kind_ == rewind_ir::IRValueKind::IR_ALLOC) {
                const auto* alloc = inst->as<rewind_ir::IRAllocInst>();
                object_slots_.emplace(inst, next_slot_offset_);
                next_slot_offset_ += alloc_size(*alloc);
                continue;
            }
            if (produces_stack_value(*inst)) {
                value_slots_.emplace(inst, next_slot_offset_);
                next_slot_offset_ += kWordSize;
            }
        }
    }

    frame_size_ = align_to(next_slot_offset_, align);
    // ra_offset_ = frame_size_ - kWordSize;
}

bool FunctionFrame::has_object_slot(const rewind_ir::IRValue* value) const
{
    return find_slot(object_slots_, value) != nullptr;
}

bool FunctionFrame::has_value_slot(const rewind_ir::IRValue* value) const
{
    return find_slot(value_slots_, value) != nullptr;
}

// return the offest of the variable in stack frame
int32_t FunctionFrame::object_slot(const rewind_ir::IRValue* value) const
{
    if (const auto* slot = find_slot(object_slots_, value)) {
        return *slot;
    }
    throw std::runtime_error("missing object slot");
}

// return the offest of the inst result in stack frame
int32_t FunctionFrame::value_slot(const rewind_ir::IRValue* value) const
{
    if (const auto* slot = find_slot(value_slots_, value)) {
        return *slot;
    }
    throw std::runtime_error("missing value slot");
}

IREmitter::IREmitter(std::ostream& out) : out_(out)
{
}

// imm12 scope [-2048, 2047]
// check value is in scope
bool IREmitter::fits_i12(int32_t value)
{
    return value >= -2048 && value <= 2047;
}

const char* IREmitter::reg_name(Register reg)
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
    }
    throw std::runtime_error("unknown register");
}

/*
 * from @name to name
 * exmple: @abc-1 -> abc_1
 */
std::string IREmitter::sanitize_symbol(std::string_view name)
{
    if (!name.empty() && (name.front() == '@' || name.front() == '%')) {
        name.remove_prefix(1);
    }

    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        // riscv name only allows letters and numbers
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

void IREmitter::emit_module(const rewind_ir::IRModule& module)
{
    if (!module.global_values_.empty()) {
        throw std::runtime_error("RISC-V backend does not support global values in this subset");
    }

    out_ << "  .text\n";
    for (const auto* func : module.funcs_) {
        out_ << "  .globl " << sanitize_symbol(func->name_) << "\n";
    }
    out_ << "\n";

    for (const auto* func : module.funcs_) {
        emit_function(*func);
    }
}

void IREmitter::emit_function(const rewind_ir::IRFunction& func)
{
    current_function_ = &func;
    frame_.build(func);

    out_ << sanitize_symbol(func.name_) << ":\n";
    emit_prologue();
    out_ << "\n";

    for (const auto* block : func.basic_blocks_) {
        emit_basic_block(*block);
    }

    out_ << "\n";
}

void IREmitter::emit_basic_block(const rewind_ir::IRBasicBlock& block)
{
    // ? dont't need to print block name
    // if (current_function_ != nullptr && !current_function_->basic_blocks_.empty() && current_function_->basic_blocks_.front() != &block) {
    // out_ << "." << sanitize_symbol(current_function_->name_) << "_"
    //<< sanitize_symbol(block.name_) << ":\n";
    //}
    const auto& bb_name = sanitize_symbol(block.name_);
    out_ << bb_name << ":\n";

    for (const auto* inst : block.insts_) {
        emit_instruction(*inst);
    }

    out_ << "\n";
}

void IREmitter::emit_instruction(const rewind_ir::IRValue& inst)
{
    switch (inst.kind_) {
    case rewind_ir::IRValueKind::IR_ALLOC:
        emit_alloc(*inst.as<rewind_ir::IRAllocInst>());
        return;
    case rewind_ir::IRValueKind::IR_STORE:
        emit_store(*inst.as<rewind_ir::IRStoreInst>());
        return;
    case rewind_ir::IRValueKind::IR_LOAD:
        emit_load(*inst.as<rewind_ir::IRLoadInst>());
        return;
    case rewind_ir::IRValueKind::IR_BINARY:
        emit_binary(*inst.as<rewind_ir::IRBinaryInst>());
        return;
    case rewind_ir::IRValueKind::IR_RETURN:
        emit_return(*inst.as<rewind_ir::IRReturnInst>());
        return;
    case rewind_ir::IRValueKind::IR_INTEGER:
        return;
    case rewind_ir::IRValueKind::IR_JUMP:
        emit_jump(*inst.as<rewind_ir::IRJumpInst>());
        return;
    case rewind_ir::IRValueKind::IR_BRANCH:
        emit_branch(*inst.as<rewind_ir::IRBranchInst>());
        return;
    default:
        break;
    }

    throw std::runtime_error("unsupported rewind IR instruction in RISC-V backend: " + inst.name_);
}

// ===== IR instruction lowering =====

// branch inst
void IREmitter::emit_branch(const rewind_ir::IRBranchInst& inst)
{
    if (frame_.has_value_slot(inst.cond_)) {
        emit_stack_load(Register::t0, frame_.value_slot(inst.cond_));
    } else if (inst.cond_->is_integer()) {
        materialize_value(inst.cond_, Register::t0);
    } else {
        throw std::runtime_error("branch condition not found");
    }

    const auto& if_label = sanitize_symbol(inst.if_basic_block_->name_);
    emit_bnez(Register::t0, if_label);
    const auto& else_label = sanitize_symbol(inst.else_basic_block_->name_);
    emit_j(else_label);
}

// jump to other inst
void IREmitter::emit_jump(const rewind_ir::IRJumpInst& inst)
{
    const auto& label = sanitize_symbol(inst.jump_basic_block_->name_);
    emit_j(label);
}

// local alloc will do nothing
void IREmitter::emit_alloc(const rewind_ir::IRAllocInst& inst)
{
    return;
}

// emit store inst
// lw reg stack_frame or li reg imm12
// sw reg stack_frame
void IREmitter::emit_store(const rewind_ir::IRStoreInst& inst)
{
    // step 1 : load value to register t0
    materialize_value(inst.value_, Register::t0);

    // step2 : check dest type
    // local variable type
    // directly store value to stack
    if (frame_.has_object_slot(inst.dest_)) {
        emit_stack_store(Register::t0, frame_.object_slot(inst.dest_));
    } else {
        throw std::runtime_error(inst.dest_->name_ + " is not variable");
    }
}

// emit load inst
// lw reg stack_frame or li reg imm12
// spill_value
void IREmitter::emit_load(const rewind_ir::IRLoadInst& inst)
{
    // check src type
    if (frame_.has_object_slot(inst.src_)) {
        // local variable
        // load value from stack
        emit_stack_load(Register::t0, frame_.object_slot(inst.src_));
    } else {
        throw std::runtime_error(inst.src_->name_ + " is not variable");
    }

    // store inst result to stack frame(inst)
    spill_value(&inst, Register::t0);
}

void IREmitter::emit_binary(const rewind_ir::IRBinaryInst& inst)
{
    materialize_value(inst.lhs_, Register::t0);
    materialize_value(inst.rhs_, Register::t1);

    switch (inst.op_) {
    case rewind_ir::IRBinaryOp::ADD:
        emit_add(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SUB:
        emit_sub(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::MUL:
        emit_mul(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::DIV:
        emit_div(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::MOD:
        emit_rem(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::AND:
        emit_and(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::OR:
        emit_or(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::XOR:
        emit_xor(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::EQ:
        emit_xor(Register::t0, Register::t0, Register::t1);
        emit_seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::NEQ:
        emit_xor(Register::t0, Register::t0, Register::t1);
        emit_snez(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::LT:
        emit_slt(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::GT:
        emit_slt(Register::t0, Register::t1, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::LE:
        emit_slt(Register::t0, Register::t1, Register::t0);
        emit_seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::GE:
        emit_slt(Register::t0, Register::t0, Register::t1);
        emit_seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::SHL:
        emit_sll(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SHR:
        emit_srl(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SAR:
        emit_sra(Register::t0, Register::t0, Register::t1);
        break;
    }

    spill_value(&inst, Register::t0);
}

// emit return inst
void IREmitter::emit_return(const rewind_ir::IRReturnInst& inst)
{
    materialize_value(inst.dst_, Register::a0);
    emit_epilogue();
    emit_ret();
}

// ===== Operand materialization helpers =====

// load value to dst register
void IREmitter::materialize_value(const rewind_ir::IRValue* value, Register dst)
{
    // maybe throw runtime error
    if (value == nullptr) {
        emit_li(dst, 0);
        return;
    }

    // immediate
    if (value->kind_ == rewind_ir::IRValueKind::IR_INTEGER) {
        emit_li(dst, value->as<rewind_ir::IRConstant>()->value_);
        return;
    }

    // inst result
    if (frame_.has_value_slot(value)) {
        emit_stack_load(dst, frame_.value_slot(value));
        return;
    }

    // variable (local variable)
    // load value from stack slot
    if (frame_.has_object_slot(value)) {
        emit_stack_load(dst, frame_.object_slot(value));
        return;
    }

    throw std::runtime_error("cannot materialize IR value: " + value->name_);
}

// store src(inst result) to stack
void IREmitter::spill_value(const rewind_ir::IRValue* value, Register src)
{
    if (!frame_.has_value_slot(value)) {
        throw std::runtime_error("missing spill slot for IR value: " + value->name_);
    }
    emit_stack_store(src, frame_.value_slot(value));
}

// ===== Stack frame helpers =====

// assgin stack frame
void IREmitter::emit_prologue()
{
    emit_adjust_sp(-frame_.frame_size());
    // emit_stack_store(Register::ra, frame_.ra_offset());
}

// recover stack frame
void IREmitter::emit_epilogue()
{
    // emit_stack_load(Register::ra, frame_.ra_offset());
    emit_adjust_sp(frame_.frame_size());
}

// adjust sp
void IREmitter::emit_adjust_sp(int32_t delta)
{
    // imm range is [-2048, 2047]
    // check if imm out of range
    if (fits_i12(delta)) {
        emit_addi(Register::sp, Register::sp, delta);
        return;
    }

    // imm out of range
    // load imm to t0 then add sp t0 to sp
    emit_li(Register::t0, delta);
    emit_add(Register::sp, Register::sp, Register::t0);
}

// ===== Stack access helpers =====

// compute stack address and store in rd
void IREmitter::emit_stack_address(Register rd, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit_addi(rd, Register::sp, offset);
        return;
    }

    if (scratch == rd) {
        throw std::runtime_error("stack address scratch register conflicts with destination register");
    }

    emit_li(scratch, offset);
    emit_add(rd, Register::sp, scratch);
}

// load stack value to rd
void IREmitter::emit_stack_load(Register rd, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit_lw(rd, Register::sp, offset);
        return;
    }

    // if imm out of range, use scrath register to store stack frame address
    emit_stack_address(scratch, offset);
    emit_lw(rd, scratch, 0);
}

// store rs to stack
void IREmitter::emit_stack_store(Register rs, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit_sw(rs, Register::sp, offset);
        return;
    }

    if (scratch == rs) {
        throw std::runtime_error("stack store scratch register conflicts with source register");
    }

    // if imm out of range, use scrath register to store stack frame address
    emit_stack_address(scratch, offset);
    emit_sw(rs, scratch, 0);
}

// ===== Raw assembly emission =====

void IREmitter::emit_li(Register rd, int32_t imm)
{
    out_ << "  li " << reg_name(rd) << ", " << imm << "\n";
}

void IREmitter::emit_mv(Register rd, Register rs)
{
    out_ << "  mv " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void IREmitter::emit_add(Register rd, Register rs1, Register rs2)
{
    out_ << "  add " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_addi(Register rd, Register rs1, int32_t imm)
{
    out_ << "  addi " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << imm << "\n";
}

void IREmitter::emit_sub(Register rd, Register rs1, Register rs2)
{
    out_ << "  sub " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_mul(Register rd, Register rs1, Register rs2)
{
    out_ << "  mul " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_div(Register rd, Register rs1, Register rs2)
{
    out_ << "  div " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_rem(Register rd, Register rs1, Register rs2)
{
    out_ << "  rem " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_and(Register rd, Register rs1, Register rs2)
{
    out_ << "  and " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_or(Register rd, Register rs1, Register rs2)
{
    out_ << "  or " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_xor(Register rd, Register rs1, Register rs2)
{
    out_ << "  xor " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_slt(Register rd, Register rs1, Register rs2)
{
    out_ << "  slt " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_sll(Register rd, Register rs1, Register rs2)
{
    out_ << "  sll " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_srl(Register rd, Register rs1, Register rs2)
{
    out_ << "  srl " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_sra(Register rd, Register rs1, Register rs2)
{
    out_ << "  sra " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_seqz(Register rd, Register rs)
{
    out_ << "  seqz " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void IREmitter::emit_snez(Register rd, Register rs)
{
    out_ << "  snez " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void IREmitter::emit_lw(Register rd, Register rs1, int32_t offset)
{
    out_ << "  lw " << reg_name(rd) << ", " << offset
         << "(" << reg_name(rs1) << ")\n";
}

void IREmitter::emit_sw(Register rs2, Register rs1, int32_t offset)
{
    out_ << "  sw " << reg_name(rs2) << ", " << offset
         << "(" << reg_name(rs1) << ")\n";
}

void IREmitter::emit_ret()
{
    out_ << "  ret\n";
}

void IREmitter::emit_bnez(Register rs, std::string label)
{
    out_ << "  bnez " << reg_name(rs) << ", " << label << "\n";
}

void IREmitter::emit_j(std::string label)
{
    out_ << "  j " << label << "\n";
}

void emit_module(const rewind_ir::IRModule& module, std::ostream& out)
{
    IREmitter emitter(out);
    emitter.emit_module(module);
}

} // namespace riscv
