#pragma once

#include "rewind_ir.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rewind_ir
{

class IRModulePass
{
public:
    virtual ~IRModulePass() = default;

    virtual std::string_view name() const = 0;
    virtual bool run(IRModule& module) = 0;
};

class IRFunctionPass
{
public:
    virtual ~IRFunctionPass() = default;

    virtual std::string_view name() const = 0;
    virtual bool run(IRFunction& function) = 0;
};

class IRNoOpModulePass final : public IRModulePass
{
public:
    std::string_view name() const override;
    bool run(IRModule& module) override;
};

class IRPassManager
{
public:
    void add_module_pass(std::unique_ptr<IRModulePass> pass);
    void add_function_pass(std::unique_ptr<IRFunctionPass> pass);

    bool run(IRModule& module);

    const std::vector<std::string>& executed_passes() const
    {
        return executed_passes_;
    }

private:
    std::vector<std::unique_ptr<IRModulePass>> module_passes_;
    std::vector<std::unique_ptr<IRFunctionPass>> function_passes_;
    std::vector<std::string> executed_passes_;
};

} // namespace rewind_ir
