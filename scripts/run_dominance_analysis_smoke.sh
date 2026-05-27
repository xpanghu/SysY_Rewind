#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/tmp/dominance-analysis-smoke"
CXX="${CXX:-${HOST_CXX:-/usr/bin/clang++}}"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/dominance_analysis_smoke.cpp" <<'CPP'
#include "cfg_analysis.h"
#include "dominance_analysis.h"
#include "ir_type.h"
#include "rewind_ir.h"

#include <algorithm>
#include <initializer_list>
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
        auto* function = module.make_function(types.getFunction({}, i32), "if_else_dom");
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
        DominanceAnalysis dom(cfg);

        if (!dom.dominates(*entry, *merge) || dom.dominates(*then_block, *merge)
            || dom.dominates(*else_block, *merge)) {
            std::cerr << "if/else dominance relation mismatch\n";
            return 1;
        }
        if (dom.immediate_dominator(*then_block) != entry
            || dom.immediate_dominator(*else_block) != entry
            || dom.immediate_dominator(*merge) != entry
            || dom.immediate_dominator(*unreachable) != nullptr) {
            std::cerr << "if/else immediate dominator mismatch\n";
            return 1;
        }
        if (!same_blocks(dom.dominator_tree_children(*entry),
                         {then_block, else_block, merge})) {
            std::cerr << "if/else dominator tree children mismatch\n";
            return 1;
        }
        if (!same_blocks(dom.dominance_frontier(*then_block), {merge})
            || !same_blocks(dom.dominance_frontier(*else_block), {merge})
            || !dom.dominance_frontier(*entry).empty()) {
            std::cerr << "if/else dominance frontier mismatch\n";
            return 1;
        }
    }

    {
        IRModule module;
        auto* function = module.make_function(types.getFunction({}, i32), "while_dom");
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
        DominanceAnalysis dom(cfg);

        if (dom.immediate_dominator(*header) != entry
            || dom.immediate_dominator(*body) != header
            || dom.immediate_dominator(*exit) != header) {
            std::cerr << "while immediate dominator mismatch\n";
            return 1;
        }
        if (!dom.dominates(*header, *body) || !dom.dominates(*header, *exit)
            || dom.dominates(*body, *exit)) {
            std::cerr << "while dominance relation mismatch\n";
            return 1;
        }
        if (!same_blocks(dom.dominator_tree_children(*header), {body, exit})) {
            std::cerr << "while dominator tree children mismatch\n";
            return 1;
        }
        if (!same_blocks(dom.dominance_frontier(*body), {header})
            || !same_blocks(dom.dominance_frontier(*header), {header})) {
            std::cerr << "while dominance frontier mismatch\n";
            return 1;
        }
    }

    return 0;
}
CPP

"${CXX}" -std=c++17 \
  -I"${ROOT_DIR}/include/ir" \
  "${ROOT_DIR}/src/ir/cfg_analysis.cpp" \
  "${ROOT_DIR}/src/ir/dominance_analysis.cpp" \
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${OUT_DIR}/dominance_analysis_smoke.cpp" \
  -o "${OUT_DIR}/dominance_analysis_smoke"

"${OUT_DIR}/dominance_analysis_smoke"

echo "Dominance analysis smoke output:"
echo "  ${OUT_DIR}/dominance_analysis_smoke"
