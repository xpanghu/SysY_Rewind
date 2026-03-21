#include "koopa_ir_printer.h"

#include <sstream>
#include <stdexcept>

namespace koopa_ir {

// Keep this printer only for quick local debugging of custom IR structs.
// For official output, go through raw program and libkoopa dump interfaces.

std::string KoopaIRPrinter::Emit(const IRProgram& program) const
{
    std::ostringstream out;
    for (size_t i = 0; i < program.functions.size(); ++i) {
        out << EmitFunction(program.functions[i]);
        if (i + 1 < program.functions.size()) {
            out << "\n";
        }
    }
    return out.str();
}

std::string KoopaIRPrinter::EmitFunction(const IRFunction& function) const
{
    std::ostringstream out;
    out << "fun @" << function.name << "(): " << function.ret_type << " {\n";
    for (const auto& block : function.blocks) {
        out << EmitBlock(block);
    }
    out << "}\n";
    return out.str();
}

std::string KoopaIRPrinter::EmitBlock(const IRBasicBlock& block) const
{
    std::ostringstream out;
    out << block.name << ":\n";
    for (const auto& inst : block.insts) {
        out << "  " << EmitInstruction(inst) << "\n";
    }
    return out.str();
}

std::string KoopaIRPrinter::EmitInstruction(const IRInstruction& inst) const
{
    if (inst.kind == IRInstruction::Kind::kRet) {
        return "ret " + std::to_string(inst.ret_value);
    }
    throw std::runtime_error("Unsupported IR instruction kind");
}

} // namespace koopa_ir
