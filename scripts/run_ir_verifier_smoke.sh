#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/tmp/ir-verifier-smoke"
CXX="${CXX:-${HOST_CXX:-/usr/bin/clang++}}"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/ir_verifier_smoke.cpp" <<'CPP'
#include "ir_type.h"
#include "ir_text_gen.h"
#include "ir_verifier.h"
#include "pass_manager.h"
#include "rewind_ir.h"

#include <iostream>
#include <memory>
#include <string>

using namespace rewind_ir;

namespace
{

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

int main()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "main");
        auto* entry = module.make_basic_block("%entry");
        module.append_basic_block(*function, *entry);
        auto* zero = module.make_value<IRConstant>(0, i32);
        auto* ret = module.make_value<IRReturnInst>(zero);
        module.append_value(*entry, *ret);

        IRVerifier verifier;
        if (!verifier.verify(module)) {
            std::cerr << verifier.report() << "\n";
            return 1;
        }

        IRPassManager pass_manager;
        pass_manager.add_module_pass(std::make_unique<IRNoOpModulePass>());
        if (pass_manager.run(module)) {
            std::cerr << "NoOp pass should not change the module\n";
            return 1;
        }

        if (!verifier.verify(module)) {
            std::cerr << verifier.report() << "\n";
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "block_arg_merge");
        auto* entry = module.make_basic_block("%entry");
        auto* then_block = module.make_basic_block("%then");
        auto* else_block = module.make_basic_block("%else");
        auto* merge = module.make_basic_block("%merge");
        module.append_basic_block(*function, *entry);
        module.append_basic_block(*function, *then_block);
        module.append_basic_block(*function, *else_block);
        module.append_basic_block(*function, *merge);

        auto* cond = module.make_value<IRConstant>(1, i32);
        auto* one = module.make_value<IRConstant>(1, i32);
        auto* two = module.make_value<IRConstant>(2, i32);
        auto* then_arg = module.make_block_param(*then_block, i32, "%then_x");
        auto* else_arg = module.make_block_param(*else_block, i32, "%else_x");
        auto* merge_arg = module.make_block_param(*merge, i32, "%x");

        if (then_arg->owner_ != then_block || else_arg->owner_ != else_block
            || merge_arg->owner_ != merge) {
            std::cerr << "block argument owner should be set by make_block_param\n";
            return 1;
        }

        auto* branch = module.make_value<IRBranchInst>(
            cond,
            then_block,
            else_block,
            unit,
            std::vector<IRValue*>{one},
            std::vector<IRValue*>{two});
        module.append_value(*entry, *branch);
        auto* then_jump =
            module.make_value<IRJumpInst>(merge, unit, std::vector<IRValue*>{then_arg});
        module.append_value(*then_block, *then_jump);
        auto* else_jump =
            module.make_value<IRJumpInst>(merge, unit, std::vector<IRValue*>{else_arg});
        module.append_value(*else_block, *else_jump);
        auto* ret = module.make_value<IRReturnInst>(merge_arg);
        module.append_value(*merge, *ret);

        IRVerifier verifier;
        if (!verifier.verify(module)) {
            std::cerr << verifier.report() << "\n";
            return 1;
        }

        IRTextGen text_gen;
        std::string text;
        if (text_gen.emit_to_string(module, text) != IRErrorCode::SUCCESS) {
            std::cerr << text_gen.last_error() << "\n";
            return 1;
        }

        if (!contains(text, "%then(%then_x: i32):")
            || !contains(text, "%else(%else_x: i32):")
            || !contains(text, "%merge(%x: i32):")
            || !contains(text, "br 1, %then(1), %else(2)")
            || !contains(text, "jump %merge(%then_x)")
            || !contains(text, "jump %merge(%else_x)")) {
            std::cerr << "block argument IR text is missing expected form:\n" << text << "\n";
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "bad_return");
        auto* entry = module.make_basic_block("%entry");
        module.append_basic_block(*function, *entry);
        auto* ret = module.make_value<IRReturnInst>(nullptr);
        module.append_value(*entry, *ret);

        IRVerifier verifier;
        if (verifier.verify(module)) {
            std::cerr << "expected invalid return IR to fail verification\n";
            return 1;
        }

        if (!contains(verifier.report(), "return")) {
            std::cerr << "invalid return report should mention return, got:\n"
                      << verifier.report() << "\n";
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "bad_block_arg_edge");
        auto* entry = module.make_basic_block("%entry");
        auto* merge = module.make_basic_block("%merge");
        module.append_basic_block(*function, *entry);
        module.append_basic_block(*function, *merge);

        auto* merge_arg = module.make_block_param(*merge, i32, "%x");
        auto* jump = module.make_value<IRJumpInst>(merge, unit);
        module.append_value(*entry, *jump);
        auto* ret = module.make_value<IRReturnInst>(merge_arg);
        module.append_value(*merge, *ret);

        IRVerifier verifier;
        if (verifier.verify(module)) {
            std::cerr << "expected missing block argument edge value to fail verification\n";
            return 1;
        }

        if (!contains(verifier.report(), "argument count")) {
            std::cerr << "missing block argument report should mention argument count, got:\n"
                      << verifier.report() << "\n";
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, unit), "empty_block");
        auto* entry = module.make_basic_block("%entry");
        module.append_basic_block(*function, *entry);

        IRVerifier verifier;
        if (verifier.verify(module)) {
            std::cerr << "expected empty block IR to fail verification\n";
            return 1;
        }

        if (!contains(verifier.report(), "terminator")) {
            std::cerr << "empty block report should mention terminator, got:\n"
                      << verifier.report() << "\n";
            return 1;
        }
    }

    return 0;
}
CPP

"${CXX}" -std=c++17 \
  -I"${ROOT_DIR}/include/ir" \
  "${ROOT_DIR}/src/ir/cfg_analysis.cpp" \
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${ROOT_DIR}/src/ir/ir_text_gen.cpp" \
  "${ROOT_DIR}/src/ir/ir_verifier.cpp" \
  "${ROOT_DIR}/src/ir/pass_manager.cpp" \
  "${OUT_DIR}/ir_verifier_smoke.cpp" \
  -o "${OUT_DIR}/ir_verifier_smoke"

"${OUT_DIR}/ir_verifier_smoke"

echo "IR verifier smoke output:"
echo "  ${OUT_DIR}/ir_verifier_smoke"
