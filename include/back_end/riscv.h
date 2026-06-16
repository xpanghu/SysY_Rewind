#pragma once

#include <iosfwd>

namespace rewind_ir
{
class IRModule;
}

namespace riscv
{

void emit_module(const rewind_ir::IRModule& module, std::ostream& out);

} // namespace riscv
