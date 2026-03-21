#pragma once

#include <string>

#include "koopa.h"
// #include "../tmp/koopa.h"

namespace koopa_ir {

std::string DumpKoopaProgramToString(koopa_program_t program);
std::string DumpLlvmToString(koopa_program_t program);

} // namespace koopa_ir
