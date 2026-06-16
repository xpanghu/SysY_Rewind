#include "riscv.h"

#include "machine_asm_printer.h"

namespace riscv
{

void emit_module(const rewind_ir::IRModule& module, std::ostream& out)
{
    MachineAsmPrinter printer(out);
    printer.emit_module(module);
}

} // namespace riscv
