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
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${ROOT_DIR}/src/ir/ir_verifier.cpp" \
  "${ROOT_DIR}/src/ir/pass_manager.cpp" \
  "${OUT_DIR}/ir_verifier_smoke.cpp" \
  -o "${OUT_DIR}/ir_verifier_smoke"

"${OUT_DIR}/ir_verifier_smoke"

echo "IR verifier smoke output:"
echo "  ${OUT_DIR}/ir_verifier_smoke"
