#pragma once
#include <unordered_map>

#include <stdexcept>
#include "../tmp/koopa.h"

#include <ostream>

namespace riscv {

// Emits RISC-V assembly from a koopa raw program.
void emit_program(const koopa_raw_program_t& program, std::ostream& out);

} // namespace riscv