#pragma once
#include <unordered_map>

#include "koopa.h"
#include <cassert>
#include <stdexcept>

#include <ostream>

namespace riscv {

// Emits RISC-V assembly from a koopa raw program.
void emit_program(const koopa_raw_program_t& program, std::ostream& out);

} // namespace riscv