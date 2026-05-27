#pragma once

#include "rewind_ir.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rewind_ir
{

class CFGAnalysis
{
public:
    explicit CFGAnalysis(const IRFunction& function);

    const IRFunction& function() const
    {
        return *function_;
    }

    const IRBasicBlock* entry() const
    {
        return entry_;
    }

    const std::vector<const IRBasicBlock*>& blocks() const
    {
        return blocks_;
    }

    const std::vector<const IRBasicBlock*>& reachable_blocks() const
    {
        return reachable_blocks_;
    }

    const std::vector<const IRBasicBlock*>& successors(const IRBasicBlock& block) const;
    const std::vector<const IRBasicBlock*>& predecessors(const IRBasicBlock& block) const;

    bool contains(const IRBasicBlock& block) const;
    bool is_reachable(const IRBasicBlock& block) const;
    bool has_edge(const IRBasicBlock& source, const IRBasicBlock& target) const;

private:
    void build();
    void add_edge(const IRBasicBlock* source, const IRBasicBlock* target);
    void compute_reachable_blocks();

    const IRFunction* function_;
    const IRBasicBlock* entry_;
    std::vector<const IRBasicBlock*> blocks_;
    std::vector<const IRBasicBlock*> reachable_blocks_;
    std::unordered_set<const IRBasicBlock*> block_set_;
    std::unordered_set<const IRBasicBlock*> reachable_set_;
    std::unordered_map<const IRBasicBlock*, std::vector<const IRBasicBlock*>> successors_;
    std::unordered_map<const IRBasicBlock*, std::vector<const IRBasicBlock*>> predecessors_;
};

} // namespace rewind_ir
