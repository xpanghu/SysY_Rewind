#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/tmp/ir-rewrite-smoke"
CXX="${CXX:-${HOST_CXX:-/usr/bin/clang++}}"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/ir_rewrite_smoke.cpp" <<'CPP'
#include "ir_rewrite.h"
#include "ir_text_gen.h"
#include "ir_type.h"
#include "ir_verifier.h"
#include "rewind_ir.h"

#include <iostream>
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
    const auto* ptr_i32 = types.getPointer(i32);

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "rewrite_load_use");
        auto* entry = module.make_basic_block("%entry");
        module.append_basic_block(*function, *entry);

        auto* slot = module.make_value<IRAllocInst>(ptr_i32, "@slot");
        auto* seven = module.make_value<IRConstant>(7, i32);
        auto* one = module.make_value<IRConstant>(1, i32);
        auto* store = module.make_value<IRStoreInst>(seven, slot, unit);
        auto* load = module.make_value<IRLoadInst>(slot, i32, "%load");
        auto* sum = module.make_value<IRBinaryInst>(IRBinaryOp::ADD, load, one, i32, "%sum");
        auto* ret = module.make_value<IRReturnInst>(sum);

        module.append_value(*entry, *slot);
        module.append_value(*entry, *store);
        module.append_value(*entry, *load);
        module.append_value(*entry, *sum);
        module.append_value(*entry, *ret);

        if (ir_rewrite::replace_all_uses(*function, load, seven) != 1) {
            std::cerr << "expected exactly one load use replacement\n";
            return 1;
        }
        if (!ir_rewrite::erase_instruction(*entry, *load)) {
            std::cerr << "expected load instruction to be erased from entry block\n";
            return 1;
        }

        IRVerifier verifier;
        if (!verifier.verify(module)) {
            std::cerr << "verifier rejected rewritten IR: " << verifier.report() << "\n";
            return 1;
        }

        std::string text;
        IRTextGen printer;
        if (printer.emit_to_string(module, text) != IRErrorCode::SUCCESS) {
            std::cerr << "failed to print rewritten IR: " << printer.last_error() << "\n";
            return 1;
        }
        if (contains(text, "%load = load") || !contains(text, "%sum = add 7, 1")) {
            std::cerr << "rewritten IR text did not contain expected replacement\n";
            std::cerr << text;
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "rewrite_edge_args");
        auto* entry = module.make_basic_block("%entry");
        auto* then_block = module.make_basic_block("%then");
        auto* else_block = module.make_basic_block("%else");
        auto* merge = module.make_basic_block("%merge");
        module.append_basic_block(*function, *entry);
        module.append_basic_block(*function, *then_block);
        module.append_basic_block(*function, *else_block);
        module.append_basic_block(*function, *merge);

        module.make_block_param(*then_block, i32, "%then_x");
        module.make_block_param(*else_block, i32, "%else_x");
        auto* merge_arg = module.make_block_param(*merge, i32, "%x");
        auto* cond = module.make_value<IRConstant>(1, i32);
        auto* one = module.make_value<IRConstant>(1, i32);
        auto* two = module.make_value<IRConstant>(2, i32);

        auto* branch = module.make_value<IRBranchInst>(
            cond,
            then_block,
            else_block,
            unit,
            std::vector<IRValue*>{one},
            std::vector<IRValue*>{two});
        module.append_value(*entry, *branch);
        module.append_value(*then_block, *module.make_value<IRJumpInst>(
            merge, unit, std::vector<IRValue*>{one}));
        module.append_value(*else_block, *module.make_value<IRJumpInst>(
            merge, unit, std::vector<IRValue*>{two}));
        module.append_value(*merge, *module.make_value<IRReturnInst>(merge_arg));

        if (!ir_rewrite::replace_operand(*branch, one, two)
            || branch->if_args_.size() != 1
            || branch->if_args_[0] != two) {
            std::cerr << "expected branch edge arg replacement through operand visitor\n";
            return 1;
        }

        IRVerifier verifier;
        if (!verifier.verify(module)) {
            std::cerr << "verifier rejected edge-arg rewrite: " << verifier.report() << "\n";
            return 1;
        }
    }

    return 0;
}
CPP

"${CXX}" -std=c++17 \
  -I"${ROOT_DIR}/include/ir" \
  "${ROOT_DIR}/src/ir/cfg_analysis.cpp" \
  "${ROOT_DIR}/src/ir/ir_rewrite.cpp" \
  "${ROOT_DIR}/src/ir/ir_text_gen.cpp" \
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${ROOT_DIR}/src/ir/ir_verifier.cpp" \
  "${OUT_DIR}/ir_rewrite_smoke.cpp" \
  -o "${OUT_DIR}/ir_rewrite_smoke"

"${OUT_DIR}/ir_rewrite_smoke"

echo "IR rewrite smoke output:"
echo "  ${OUT_DIR}/ir_rewrite_smoke"
