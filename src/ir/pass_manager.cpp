#include "pass_manager.h"
#include <stdexcept>

namespace rewind_ir
{

std::string_view IRNoOpModulePass::name() const
{
    return "noop-module";
}

bool IRNoOpModulePass::run(IRModule&)
{
    return false;
}

void IRPassManager::add_module_pass(std::unique_ptr<IRModulePass> pass)
{
    if (!pass) {
        throw std::runtime_error("module pass cannot be null");
    }
    module_passes_.push_back(std::move(pass));
}

void IRPassManager::add_function_pass(std::unique_ptr<IRFunctionPass> pass)
{
    if (!pass) {
        throw std::runtime_error("function pass cannot be null");
    }
    function_passes_.push_back(std::move(pass));
}

bool IRPassManager::run(IRModule& module)
{
    executed_passes_.clear();
    bool changed = false;

    for (const auto& pass : module_passes_) {
        executed_passes_.push_back(std::string(pass->name()));
        changed = pass->run(module) || changed;
    }

    for (auto* function : module.funcs_) {
        if (function == nullptr || function->is_declaration_) {
            continue;
        }

        for (const auto& pass : function_passes_) {
            executed_passes_.push_back(std::string(pass->name()) + ":@" + function->name_);
            changed = pass->run(*function) || changed;
        }
    }

    return changed;
}

} // namespace rewind_ir
