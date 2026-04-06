#pragma once

#include "rewind_ir.h"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>

namespace riscv
{

enum class Register {
    x0, // always zero, and
    ra, // save return address
    sp, // stack pointer
    t0, // t0 - t6 temporary register
    t1,
    t2,
    a0, // save return parameters
};

class FunctionFrame
{
public:
    void build(const rewind_ir::IRFunction& func);

    bool has_object_slot(const rewind_ir::IRValue* value) const;
    bool has_value_slot(const rewind_ir::IRValue* value) const;

    int32_t object_slot(const rewind_ir::IRValue* value) const;
    int32_t value_slot(const rewind_ir::IRValue* value) const;

    int32_t frame_size() const
    {
        return frame_size_;
    }

    int32_t ra_offset() const
    {
        return ra_offset_;
    }

private:
    static bool produces_stack_value(const rewind_ir::IRValue& value);
    static int32_t alloc_size(const rewind_ir::IRAllocInst& inst);
    static int32_t align_to(int32_t value, int32_t align);

    int32_t next_slot_offset_ = 0;
    int32_t frame_size_ = 16; // the size of stack frame
    int32_t ra_offset_ = 12;
    std::unordered_map<const rewind_ir::IRValue*, int32_t> object_slots_; // represent the stack offest of the variable
    std::unordered_map<const rewind_ir::IRValue*, int32_t> value_slots_;  // represent the stack offest of the inst value
};

/*
 * lw rs, imm12(rd) : read mem(rd + imm12) , then store to rs
 * sw rs, imm12(rd) : store value of rs  to mem(rd + imm12)
 */
class IREmitter
{
public:
    explicit IREmitter(std::ostream& out);

    void emit_module(const rewind_ir::IRModule& module);

private:
    void emit_function(const rewind_ir::IRFunction& func);
    void emit_basic_block(const rewind_ir::IRBasicBlock& block);
    void emit_instruction(const rewind_ir::IRValue& inst);

    void emit_alloc(const rewind_ir::IRAllocInst& inst);
    void emit_store(const rewind_ir::IRStoreInst& inst);
    void emit_load(const rewind_ir::IRLoadInst& inst);
    void emit_binary(const rewind_ir::IRBinaryInst& inst);
    void emit_return(const rewind_ir::IRReturnInst& inst);

    // Calculate the stack memory size of a function and aligned to 16 btypes
    void emit_prologue();
    void emit_epilogue();

    void materialize_value(const rewind_ir::IRValue* value, Register dst);
    void materialize_pointer(const rewind_ir::IRValue* value, Register dst);
    void spill_value(const rewind_ir::IRValue* value, Register src);

    void emit_adjust_sp(int32_t delta);
    void emit_stack_address(Register rd, int32_t offset);
    void emit_stack_load(Register rd, int32_t offset, Register scratch = Register::t2);
    void emit_stack_store(Register rs, int32_t offset, Register scratch = Register::t2);

    void emit_li(Register rd, int32_t imm);
    void emit_mv(Register rd, Register rs);
    void emit_add(Register rd, Register rs1, Register rs2);
    void emit_addi(Register rd, Register rs1, int32_t imm);
    void emit_sub(Register rd, Register rs1, Register rs2);
    void emit_mul(Register rd, Register rs1, Register rs2);
    void emit_div(Register rd, Register rs1, Register rs2);
    void emit_rem(Register rd, Register rs1, Register rs2);
    void emit_and(Register rd, Register rs1, Register rs2);
    void emit_or(Register rd, Register rs1, Register rs2);
    void emit_xor(Register rd, Register rs1, Register rs2);
    void emit_slt(Register rd, Register rs1, Register rs2);
    void emit_seqz(Register rd, Register rs);
    void emit_snez(Register rd, Register rs);
    void emit_lw(Register rd, Register rs1, int32_t offset);
    void emit_sw(Register rs2, Register rs1, int32_t offset);
    void emit_ret();

    static bool fits_i12(int32_t value);
    static const char* reg_name(Register reg);
    static std::string sanitize_symbol(std::string_view name);

    std::ostream& out_;
    const rewind_ir::IRFunction* current_function_ = nullptr;
    FunctionFrame frame_;
};

void emit_module(const rewind_ir::IRModule& module, std::ostream& out);

} // namespace riscv
