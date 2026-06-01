#pragma once

#include "asm_writer.h"
#include "data_layout.h"
#include "machine_ir.h"
#include "rewind_ir.h"
#include <iosfwd>

namespace riscv
{

class MachineAsmPrinter
{
public:
    explicit MachineAsmPrinter(std::ostream& out);

    void emit_module(const rewind_ir::IRModule& module);
    void emit_function(const MachineFunction& function);
    void emit_basic_block(const MachineBasicBlock& block);
    void emit_instruction(const MachineInstr& inst);

private:
    void emit_global_value(const rewind_ir::IRGlobalAllocInst& global_alloc);
    void emit_global_initializer(const rewind_ir::IRValue* init, const rewind_ir::IRType* type);

    AsmWriter writer_;
    DataLayout data_layout_;
};

} // namespace riscv
