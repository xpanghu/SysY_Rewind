#pragma once

#include "asm_writer.h"
#include "calling_convention.h"
#include "data_layout.h"
#include "frame_layout.h"
#include "rewind_ir.h"
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace riscv
{

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
    void emit_get_ptr(const rewind_ir::IRGetPtrInst& inst);
    void emit_get_elem_ptr(const rewind_ir::IRGetElemPtrInst& inst);
    void emit_global_initializer(const rewind_ir::IRValue* init, const rewind_ir::IRType* type);
    void emit_initializer_store(const rewind_ir::IRValue* init,
                                const rewind_ir::IRType* type,
                                Register base,
                                int32_t offset);

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
    void emit_stack_address(Register rd, int32_t offset);
    void emit_stack_load(Register rd, int32_t offset);
    void emit_stack_store(Register rs, int32_t offset, Register scratch = Register::t2);

    std::string basic_block_label(const rewind_ir::IRBasicBlock& block) const;

    static bool fits_i12(int32_t value);
    static std::string sanitize_symbol(std::string_view name);

    AsmWriter writer_;
    DataLayout data_layout_;
    CallingConvention calling_convention_;
    const rewind_ir::IRFunction* current_function_ = nullptr;
    FrameLayout frame_;
};

void emit_module(const rewind_ir::IRModule& module, std::ostream& out);

} // namespace riscv
