#pragma once

#include "calling_convention.h"
#include "data_layout.h"
#include "frame_layout.h"
#include "machine_ir.h"
#include "rewind_ir.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace riscv
{

class InstructionSelector
{
public:
    MachineFunction select_function(const rewind_ir::IRFunction& func);

private:
    void select_basic_block(const rewind_ir::IRBasicBlock& block);
    void select_instruction(const rewind_ir::IRValue& inst);

    void select_alloc(const rewind_ir::IRAllocInst& inst);
    void select_store(const rewind_ir::IRStoreInst& inst);
    void select_load(const rewind_ir::IRLoadInst& inst);
    void select_binary(const rewind_ir::IRBinaryInst& inst);
    void select_call(const rewind_ir::IRCallInst& inst);
    void select_branch(const rewind_ir::IRBranchInst& inst);
    void select_jump(const rewind_ir::IRJumpInst& inst);
    void select_return(const rewind_ir::IRReturnInst& inst);
    void select_get_ptr(const rewind_ir::IRGetPtrInst& inst);
    void select_get_elem_ptr(const rewind_ir::IRGetElemPtrInst& inst);
    void select_initializer_store(const rewind_ir::IRValue* init,
                                  const rewind_ir::IRType* type,
                                  Register base,
                                  int32_t offset);

    void materialize_value(const rewind_ir::IRValue* value, Register dst);
    void store_to_addressable(const rewind_ir::IRValue* value, Register src);
    void materialize_pointer(const rewind_ir::IRValue* value, Register dst);
    void spill_value(const rewind_ir::IRValue* value, Register src);
    void select_edge_args(const rewind_ir::IRBasicBlock& target,
                          const std::vector<rewind_ir::IRValue*>& args);

    void select_prologue();
    void select_epilogue();
    void select_adjust_sp(int32_t delta);

    void select_stack_address(Register rd, int32_t offset);
    void select_stack_load(Register rd, int32_t offset);
    void select_stack_store(Register rs, int32_t offset, Register scratch = Register::t2);

    void emit(MachineInstr instr);

    std::string basic_block_label(const rewind_ir::IRBasicBlock& block) const;
    std::string edge_label(const rewind_ir::IRBasicBlock& from,
                           const rewind_ir::IRBasicBlock& to,
                           std::string_view suffix) const;

    static bool fits_i12(int32_t value);
    static std::string sanitize_symbol(std::string_view name);

    DataLayout data_layout_;
    CallingConvention calling_convention_;
    FrameLayout frame_;
    const rewind_ir::IRFunction* current_function_ = nullptr;
    const rewind_ir::IRBasicBlock* current_ir_block_ = nullptr;
    MachineFunction* current_machine_function_ = nullptr;
    MachineBasicBlock* current_machine_block_ = nullptr;
};

} // namespace riscv
