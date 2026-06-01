#pragma once

#include "machine_ir.h"
#include <string>

namespace riscv
{

class MachineVerifier
{
public:
    bool verify(const MachineFunction& function);

    const std::string& report() const
    {
        return report_;
    }

private:
    void clear();
    bool fail(std::string message);

    std::string report_;
};

} // namespace riscv
