#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/tmp/cfg-analysis-smoke"
CXX="${CXX:-${HOST_CXX:-/usr/bin/clang++}}"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/cfg_analysis_smoke.cpp" <<'CPP'
#include "cfg_analysis.h"
#include "ir_type.h"
#include "rewind_ir.h"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace rewind_ir;

namespace
{

bool same_blocks(const std::vector<const IRBasicBlock*>& actual,
                 std::initializer_list<const IRBasicBlock*> expected)
{
    return actual.size() == expected.size()
        && std::equal(actual.begin(), actual.end(), expected.begin());
}

} // namespace

int main()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "if_else_cfg");
        auto* entry = module.make_basic_block("%entry");
        auto* then_block = module.make_basic_block("%then");
        auto* else_block = module.make_basic_block("%else");
        auto* merge = module.make_basic_block("%merge");
        auto* unreachable = module.make_basic_block("%dead");
        module.append_basic_block(*function, *entry);
        module.append_basic_block(*function, *then_block);
        module.append_basic_block(*function, *else_block);
        module.append_basic_block(*function, *merge);
        module.append_basic_block(*function, *unreachable);

        auto* cond = module.make_value<IRConstant>(1, i32);
        auto* zero = module.make_value<IRConstant>(0, i32);
        module.append_value(*entry, *module.make_value<IRBranchInst>(
            cond, then_block, else_block, unit));
        module.append_value(*then_block, *module.make_value<IRJumpInst>(merge, unit));
        module.append_value(*else_block, *module.make_value<IRJumpInst>(merge, unit));
        module.append_value(*merge, *module.make_value<IRReturnInst>(zero));
        module.append_value(*unreachable, *module.make_value<IRReturnInst>(zero));

        CFGAnalysis cfg(*function);

        if (cfg.entry() != entry) {
            std::cerr << "entry block mismatch\n";
            return 1;
        }
        if (!same_blocks(cfg.successors(*entry), {then_block, else_block})) {
            std::cerr << "entry successors mismatch\n";
            return 1;
        }
        if (!same_blocks(cfg.predecessors(*merge), {then_block, else_block})) {
            std::cerr << "merge predecessors mismatch\n";
            return 1;
        }
        if (!same_blocks(cfg.reachable_blocks(), {entry, then_block, else_block, merge})) {
            std::cerr << "reachable block order mismatch\n";
            return 1;
        }
        if (cfg.is_reachable(*unreachable)) {
            std::cerr << "unreachable block should not be reachable\n";
            return 1;
        }
        if (!cfg.has_edge(*entry, *then_block) || !cfg.has_edge(*entry, *else_block)
            || !cfg.has_edge(*then_block, *merge) || !cfg.has_edge(*else_block, *merge)) {
            std::cerr << "expected CFG edge missing\n";
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "while_cfg");
        auto* entry = module.make_basic_block("%entry");
        auto* header = module.make_basic_block("%while_entry");
        auto* body = module.make_basic_block("%while_body");
        auto* exit = module.make_basic_block("%end");
        module.append_basic_block(*function, *entry);
        module.append_basic_block(*function, *header);
        module.append_basic_block(*function, *body);
        module.append_basic_block(*function, *exit);

        auto* cond = module.make_value<IRConstant>(1, i32);
        auto* zero = module.make_value<IRConstant>(0, i32);
        module.append_value(*entry, *module.make_value<IRJumpInst>(header, unit));
        module.append_value(*header, *module.make_value<IRBranchInst>(
            cond, body, exit, unit));
        module.append_value(*body, *module.make_value<IRJumpInst>(header, unit));
        module.append_value(*exit, *module.make_value<IRReturnInst>(zero));

        CFGAnalysis cfg(*function);

        if (!same_blocks(cfg.successors(*header), {body, exit})) {
            std::cerr << "while header successors mismatch\n";
            return 1;
        }
        if (!same_blocks(cfg.predecessors(*header), {entry, body})) {
            std::cerr << "while header predecessors mismatch\n";
            return 1;
        }
        if (!cfg.has_edge(*body, *header)) {
            std::cerr << "while back edge missing\n";
            return 1;
        }
    }

    return 0;
}
CPP

"${CXX}" -std=c++17 \
  -I"${ROOT_DIR}/include/ir" \
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${ROOT_DIR}/src/ir/cfg_analysis.cpp" \
  "${OUT_DIR}/cfg_analysis_smoke.cpp" \
  -o "${OUT_DIR}/cfg_analysis_smoke"

"${OUT_DIR}/cfg_analysis_smoke"

echo "CFG analysis smoke output:"
echo "  ${OUT_DIR}/cfg_analysis_smoke"
