#include "machine_verifier.h"

#include <sstream>

namespace riscv
{

void MachineVerifier::clear()
{
    report_.clear();
}

bool MachineVerifier::fail(std::string message)
{
    report_ = std::move(message);
    return false;
}

bool MachineVerifier::verify(const MachineFunction& function)
{
    clear();

    if (function.blocks().empty()) {
        return fail("machine function has no basic blocks: " + function.name());
    }

    for (const auto& block : function.blocks()) {
        if (block.instrs().empty()) {
            return fail("machine basic block missing terminator: " + block.label());
        }

        const auto& last = block.instrs().back();
        if (!last.is_terminator()) {
            return fail("machine basic block missing terminator: " + block.label());
        }
    }

    return true;
}

} // namespace riscv
