#pragma once

#include "ir_type.h"
#include "rewind_ir.h"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>

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

    bool has_saved_ra() const
    {
        return has_saved_ra_;
    }

    int32_t outgoing_arg_size() const
    {
        return outgoing_arg_size_;
    }

    int32_t outgoing_arg_offset(size_t arg_index) const;
    int32_t incoming_stack_arg_offset(size_t arg_index) const;

private:
    static const int32_t* find_slot(
        const std::unordered_map<const rewind_ir::IRValue*, int32_t>& slots,
        const rewind_ir::IRValue* value);
    static int32_t stack_storage_size(const rewind_ir::IRValue& value);
    static int32_t align_to(int32_t value, int32_t align);

    int32_t next_slot_offset_ = 0;
    int32_t frame_size_ = 0; // the size of function stack frame
    int32_t ra_offset_ = 0;  // return address offset
    int32_t outgoing_arg_size_ = 0;
    bool has_saved_ra_ = false;

    std::unordered_map<const rewind_ir::IRValue*, int32_t> object_slots_; // represent the stack offest of the variable (scalar and array)
    std::unordered_map<const rewind_ir::IRValue*, int32_t> value_slots_;  // represent the stack offest of the intst result
};

/*
 * lw rs, imm12(rd) : read the value of mem(rd + imm12) , then store to rs
 * sw rs, imm12(rd) : store the value of rs to mem(rd + imm12)
 */
class IREmitter
{
public:
    explicit IREmitter(std::ostream& out);

    void emit_module(const rewind_ir::IRModule& module);

private:
    // IR traversal
    void emit_function(const rewind_ir::IRFunction& func);
    void emit_global_value(const rewind_ir::IRGlobalAllocInst& global_alloc);
    void emit_basic_block(const rewind_ir::IRBasicBlock& block);
    void emit_instruction(const rewind_ir::IRValue& inst);

    // IR instruction lowering
    void emit_alloc(const rewind_ir::IRAllocInst& inst);
    void emit_store(const rewind_ir::IRStoreInst& inst);
    void emit_load(const rewind_ir::IRLoadInst& inst);
    void emit_binary(const rewind_ir::IRBinaryInst& inst);
    void emit_call(const rewind_ir::IRCallInst& inst);
    void emit_branch(const rewind_ir::IRBranchInst& inst);
    void emit_jump(const rewind_ir::IRJumpInst& inst);
    void emit_return(const rewind_ir::IRReturnInst& inst);
    void emit_get_elem_ptr(const rewind_ir::IRGetElemPtrInst& inst);

    /*
     * if you only have a IRValue*, use materialize_value
     * if you know the frame offset, just use emit_stack_*
     */
    // Operand materialization helpers
    void materialize_value(const rewind_ir::IRValue* value, Register dst);
    void store_to_addressable(const rewind_ir::IRValue* value, Register src);
    void materialize_pointer(const rewind_ir::IRValue* value, Register dst);
    void spill_value(const rewind_ir::IRValue* value, Register src);

    // Stack frame helpers
    void emit_prologue();
    void emit_epilogue();
    void emit_adjust_sp(int32_t delta);

    // Stack access helpers
    void emit_stack_address(Register rd, int32_t offset, Register scratch = Register::t2);
    void emit_stack_load(Register rd, int32_t offset, Register scratch = Register::t2);
    void emit_stack_store(Register rs, int32_t offset, Register scratch = Register::t2);

    // Raw assembly emission
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
    void emit_sll(Register rd, Register rs1, Register rs2);
    void emit_srl(Register rd, Register rs1, Register rs2);
    void emit_sra(Register rd, Register rs1, Register rs2);
    void emit_seqz(Register rd, Register rs);
    void emit_snez(Register rd, Register rs);
    void emit_la(Register rd, const std::string& label);
    void emit_lw(Register rd, Register rs1, int32_t offset);
    void emit_sw(Register rs2, Register rs1, int32_t offset);
    void emit_ret();
    void emit_call_label(const std::string& label);
    void emit_bnez(Register rs, const std::string& label);
    void emit_j(const std::string& label);
    std::string basic_block_label(const rewind_ir::IRBasicBlock& block) const;

    static bool fits_i12(int32_t value);
    static const char* reg_name(Register reg);
    static Register arg_reg(size_t index);
    static std::string sanitize_symbol(std::string_view name);

    std::ostream& out_;
    const rewind_ir::IRFunction* current_function_ = nullptr;
    FunctionFrame frame_;
};

void emit_module(const rewind_ir::IRModule& module, std::ostream& out);

} // namespace riscv
