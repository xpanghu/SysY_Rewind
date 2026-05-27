#include "cfg_analysis.h"
#include <algorithm>

namespace rewind_ir
{
namespace
{

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

const IRValue* terminator_of(const IRBasicBlock& block)
{
    if (block.insts_.empty()) {
        return nullptr;
    }

    return block.insts_.back();
}

} // namespace

CFGAnalysis::CFGAnalysis(const IRFunction& function) :
    function_(&function),
    entry_(nullptr)
{
    build();
}

const std::vector<const IRBasicBlock*>& CFGAnalysis::successors(const IRBasicBlock& block) const
{
    auto iter = successors_.find(&block);
    if (iter == successors_.end()) {
        return empty_blocks;
    }
    return iter->second;
}

const std::vector<const IRBasicBlock*>& CFGAnalysis::predecessors(const IRBasicBlock& block) const
{
    auto iter = predecessors_.find(&block);
    if (iter == predecessors_.end()) {
        return empty_blocks;
    }
    return iter->second;
}

bool CFGAnalysis::contains(const IRBasicBlock& block) const
{
    return block_set_.find(&block) != block_set_.end();
}

bool CFGAnalysis::is_reachable(const IRBasicBlock& block) const
{
    return reachable_set_.find(&block) != reachable_set_.end();
}

bool CFGAnalysis::has_edge(const IRBasicBlock& source, const IRBasicBlock& target) const
{
    const auto& edges = successors(source);
    return std::find(edges.begin(), edges.end(), &target) != edges.end();
}

void CFGAnalysis::build()
{
    for (const auto* block : function_->basic_blocks_) {
        if (block == nullptr || block_set_.find(block) != block_set_.end()) {
            continue;
        }

        block_set_.insert(block);
        blocks_.push_back(block);
        successors_[block] = {};
        predecessors_[block] = {};
    }

    if (!blocks_.empty()) {
        entry_ = blocks_.front();
    }

    for (const auto* block : blocks_) {
        const auto* terminator = terminator_of(*block);
        if (terminator == nullptr) {
            continue;
        }

        switch (terminator->kind_) {
        case IRValueKind::IR_BRANCH: {
            const auto* branch = terminator->as<IRBranchInst>();
            add_edge(block, branch->if_basic_block_);
            add_edge(block, branch->else_basic_block_);
            break;
        }
        case IRValueKind::IR_JUMP: {
            const auto* jump = terminator->as<IRJumpInst>();
            add_edge(block, jump->jump_basic_block_);
            break;
        }
        default:
            break;
        }
    }

    compute_reachable_blocks();
}

void CFGAnalysis::add_edge(const IRBasicBlock* source, const IRBasicBlock* target)
{
    if (source == nullptr || target == nullptr
        || block_set_.find(source) == block_set_.end()
        || block_set_.find(target) == block_set_.end()) {
        return;
    }

    append_unique(successors_[source], target);
    append_unique(predecessors_[target], source);
}

void CFGAnalysis::compute_reachable_blocks()
{
    if (entry_ == nullptr) {
        return;
    }

    reachable_set_.insert(entry_);
    reachable_blocks_.push_back(entry_);

    for (size_t i = 0; i < reachable_blocks_.size(); ++i) {
        const auto* block = reachable_blocks_[i];
        for (const auto* successor : successors(*block)) {
            if (successor == nullptr || reachable_set_.find(successor) != reachable_set_.end()) {
                continue;
            }

            reachable_set_.insert(successor);
            reachable_blocks_.push_back(successor);
        }
    }
}

} // namespace rewind_ir
