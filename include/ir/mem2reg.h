#pragma once

#include "pass_manager.h"

namespace rewind_ir
{

class Mem2RegPass final : public IRModulePass
{
public:
    std::string_view name() const override;
    bool run(IRModule& module) override;

private:
    bool run_function(IRModule& module, IRFunction& function);
};

} // namespace rewind_ir
