#pragma once

#include "rewind_ir.h"
#include <string>
#include <vector>

namespace rewind_ir
{

class IRVerifier
{
public:
    bool verify(const IRModule& module);

    const std::vector<std::string>& errors() const
    {
        return errors_;
    }

    std::string report() const;

private:
    void error(std::string message);

    std::vector<std::string> errors_;
};

void verify_or_throw(const IRModule& module);

} // namespace rewind_ir
