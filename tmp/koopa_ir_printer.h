#pragma once

#include <string>

#include "../src/koopa_ir.h"

namespace koopa_ir {

// Debug-only fallback printer.
// Deprecated: use KoopaRawBuilder + libkoopa dump APIs for production output.
class [[deprecated("Use KoopaRawBuilder + libkoopa dump APIs instead")]] KoopaIRPrinter {
public:
    std::string Emit(const IRProgram& program) const;

private:
    std::string EmitFunction(const IRFunction& function) const;
    std::string EmitBlock(const IRBasicBlock& block) const;
    std::string EmitInstruction(const IRInstruction& inst) const;
};

} // namespace koopa_ir
