#include "dominance_analysis.h"
#include <algorithm>

namespace rewind_ir
{
namespace
{

using BlockSet = std::unordered_set<const IRBasicBlock*>;

const std::vector<const IRBasicBlock*> empty_blocks;

void append_unique(std::vector<const IRBasicBlock*>& blocks, const IRBasicBlock* block)
{
    if (block == nullptr) {
        return;
    }

    if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
        blocks.push_back(block);
    }
}

bool same_set(const BlockSet& lhs, const BlockSet& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (const auto* item : lhs) {
        if (rhs.find(item) == rhs.end()) {
            return false;
        }
    }
    return true;
}

BlockSet intersect_sets(const BlockSet& lhs, const BlockSet& rhs)
{
    BlockSet result;
    for (const auto* item : lhs) {
        if (rhs.find(item) != rhs.end()) {
            result.insert(item);
        }
    }
    return result;
}

} // namespace

DominanceAnalysis::DominanceAnalysis(const CFGAnalysis& cfg) :
    cfg_(&cfg)
{
    build();
}

bool DominanceAnalysis::dominates(const IRBasicBlock& dominator, const IRBasicBlock& block) const
{
    auto iter = dominator_sets_.find(&block);
    if (iter == dominator_sets_.end()) {
        return false;
    }

    return iter->second.find(&dominator) != iter->second.end();
}

bool DominanceAnalysis::strictly_dominates(const IRBasicBlock& dominator,
                                           const IRBasicBlock& block) const
{
    return &dominator != &block && dominates(dominator, block);
}

const IRBasicBlock* DominanceAnalysis::immediate_dominator(const IRBasicBlock& block) const
{
    auto iter = immediate_dominators_.find(&block);
    if (iter == immediate_dominators_.end()) {
        return nullptr;
    }
    return iter->second;
}

const std::vector<const IRBasicBlock*>&
DominanceAnalysis::dominator_tree_children(const IRBasicBlock& block) const
{
    auto iter = dominator_tree_children_.find(&block);
    if (iter == dominator_tree_children_.end()) {
        return empty_blocks;
    }
    return iter->second;
}

const std::vector<const IRBasicBlock*>&
DominanceAnalysis::dominance_frontier(const IRBasicBlock& block) const
{
    auto iter = dominance_frontiers_.find(&block);
    if (iter == dominance_frontiers_.end()) {
        return empty_blocks;
    }
    return iter->second;
}

void DominanceAnalysis::build()
{
    initialize_dominator_sets();
    compute_dominator_sets();
    compute_immediate_dominators();
    compute_dominator_tree_children();
    compute_dominance_frontier();
}

void DominanceAnalysis::initialize_dominator_sets()
{
    BlockSet all_reachable_blocks;
    for (const auto* block : cfg_->reachable_blocks()) {
        all_reachable_blocks.insert(block);
        immediate_dominators_[block] = nullptr;
        dominator_tree_children_[block] = {};
        dominance_frontiers_[block] = {};
    }

    const auto* entry = cfg_->entry();
    for (const auto* block : cfg_->reachable_blocks()) {
        if (block == entry) {
            dominator_sets_[block] = BlockSet{block};
        } else {
            dominator_sets_[block] = all_reachable_blocks;
        }
    }
}

void DominanceAnalysis::compute_dominator_sets()
{
    const auto* entry = cfg_->entry();
    bool changed = true;

    while (changed) {
        changed = false;
        for (const auto* block : cfg_->reachable_blocks()) {
            if (block == entry) {
                continue;
            }

            BlockSet new_dominators;
            bool has_reachable_predecessor = false;
            for (const auto* predecessor : cfg_->predecessors(*block)) {
                if (predecessor == nullptr || !cfg_->is_reachable(*predecessor)) {
                    continue;
                }

                if (!has_reachable_predecessor) {
                    new_dominators = dominator_sets_[predecessor];
                    has_reachable_predecessor = true;
                } else {
                    new_dominators = intersect_sets(new_dominators,
                                                    dominator_sets_[predecessor]);
                }
            }

            if (!has_reachable_predecessor) {
                new_dominators.clear();
            }
            new_dominators.insert(block);

            if (!same_set(new_dominators, dominator_sets_[block])) {
                dominator_sets_[block] = std::move(new_dominators);
                changed = true;
            }
        }
    }
}

void DominanceAnalysis::compute_immediate_dominators()
{
    const auto* entry = cfg_->entry();
    for (const auto* block : cfg_->reachable_blocks()) {
        if (block == entry) {
            immediate_dominators_[block] = nullptr;
            continue;
        }

        std::vector<const IRBasicBlock*> strict_dominators;
        for (const auto* candidate : cfg_->reachable_blocks()) {
            if (candidate != block && dominates(*candidate, *block)) {
                strict_dominators.push_back(candidate);
            }
        }

        for (const auto* candidate : strict_dominators) {
            bool dominated_by_all_other_strict_dominators = true;
            for (const auto* other : strict_dominators) {
                if (other == candidate) {
                    continue;
                }

                if (!dominates(*other, *candidate)) {
                    dominated_by_all_other_strict_dominators = false;
                    break;
                }
            }

            if (dominated_by_all_other_strict_dominators) {
                immediate_dominators_[block] = candidate;
                break;
            }
        }
    }
}

void DominanceAnalysis::compute_dominator_tree_children()
{
    for (const auto* block : cfg_->reachable_blocks()) {
        const auto* idom = immediate_dominator(*block);
        if (idom == nullptr) {
            continue;
        }

        append_unique(dominator_tree_children_[idom], block);
    }
}

void DominanceAnalysis::compute_dominance_frontier()
{
    for (const auto* block : cfg_->reachable_blocks()) {
        const auto* stop = immediate_dominator(*block);
        if (stop == nullptr) {
            continue;
        }


        for (const auto* predecessor : cfg_->predecessors(*block)) {
            if (predecessor == nullptr || !cfg_->is_reachable(*predecessor)) {
                continue;
            }

            auto* runner = predecessor;
            while (runner != nullptr && runner != stop) {
                append_unique(dominance_frontiers_[runner], block);
                runner = immediate_dominator(*runner);
            }
        }
    }
}

} // namespace rewind_ir
