#pragma once

#include "cfg_analysis.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rewind_ir
{

class DominanceAnalysis
{
public:
    explicit DominanceAnalysis(const CFGAnalysis& cfg);

    const CFGAnalysis& cfg() const
    {
        return *cfg_;
    }

    bool dominates(const IRBasicBlock& dominator, const IRBasicBlock& block) const;
    bool strictly_dominates(const IRBasicBlock& dominator, const IRBasicBlock& block) const;

    const IRBasicBlock* immediate_dominator(const IRBasicBlock& block) const;
    const std::vector<const IRBasicBlock*>& dominator_tree_children(const IRBasicBlock& block) const;
    const std::vector<const IRBasicBlock*>& dominance_frontier(const IRBasicBlock& block) const;

private:
    using BlockSet = std::unordered_set<const IRBasicBlock*>;

    void build();
    void initialize_dominator_sets();
    void compute_dominator_sets();
    void compute_immediate_dominators();
    void compute_dominator_tree_children();
    void compute_dominance_frontier();

    const CFGAnalysis* cfg_;
    std::unordered_map<const IRBasicBlock*, BlockSet> dominator_sets_;
    std::unordered_map<const IRBasicBlock*, const IRBasicBlock*> immediate_dominators_;
    std::unordered_map<const IRBasicBlock*, std::vector<const IRBasicBlock*>> dominator_tree_children_;
    std::unordered_map<const IRBasicBlock*, std::vector<const IRBasicBlock*>> dominance_frontiers_;
};

} // namespace rewind_ir
