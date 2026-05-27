#include "mem2reg.h"

#include "cfg_analysis.h"
#include "dominance_analysis.h"
#include "ir_rewrite.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rewind_ir
{
namespace
{

struct AllocaInfo
{
    IRAllocInst* alloc = nullptr;
    std::vector<IRLoadInst*> loads;
    std::vector<IRStoreInst*> stores;
    std::unordered_set<const IRBasicBlock*> def_blocks;
    std::unordered_set<const IRBasicBlock*> phi_blocks;
    std::unordered_map<const IRBasicBlock*, IRBlockArgRef*> block_params;
};

struct PromotionState
{
    IRFunction& function;
    const DominanceAnalysis& dominance;
    const std::vector<AllocaInfo*>& allocas;
    std::unordered_map<const IRValue*, AllocaInfo*> info_by_alloc;
    std::unordered_map<const IRValue*, AllocaInfo*> info_by_block_param;
};

const IRType* pointer_base_type(const IRValue* value)
{
    if (value == nullptr || value->type_ == nullptr || !value->type_->is_pointer()) {
        return nullptr;
    }
    return value->type_->as<IRPointerType>()->base_type;
}

bool is_promotable_alloc_type(const IRValue* value)
{
    if (value == nullptr || value->kind_ != IRValueKind::IR_ALLOC) {
        return false;
    }

    const auto* base = pointer_base_type(value);
    return base != nullptr && base->is_int32();
}

IRAllocInst* as_candidate_alloc(IRValue* value,
                                const std::unordered_map<const IRValue*, AllocaInfo*>& infos)
{
    const auto iter = infos.find(value);
    if (iter == infos.end()) {
        return nullptr;
    }
    return iter->second->alloc;
}

std::string make_block_param_name(const IRValue& alloc, size_t index)
{
    std::string base = alloc.name_;
    for (auto& ch : base) {
        if (ch == '@' || ch == '%') {
            ch = '_';
        }
    }

    if (base.empty()) {
        base = "alloc";
    }

    return "%mem2reg" + base + "_" + std::to_string(index);
}

std::vector<std::unique_ptr<AllocaInfo>>
collect_alloca_infos(IRFunction& function,
                     const CFGAnalysis& cfg,
                     std::unordered_map<const IRValue*, AllocaInfo*>& info_by_alloc)
{
    std::vector<std::unique_ptr<AllocaInfo>> infos;

    for (auto* block : function.basic_blocks_) {
        if (block == nullptr || !cfg.is_reachable(*block)) {
            continue;
        }

        for (auto* inst : block->insts_) {
            if (!is_promotable_alloc_type(inst)) {
                continue;
            }

            auto info = std::make_unique<AllocaInfo>();
            info->alloc = inst->as<IRAllocInst>();
            info_by_alloc[info->alloc] = info.get();
            infos.push_back(std::move(info));
        }
    }

    return infos;
}

void collect_direct_uses(IRFunction& function,
                         const CFGAnalysis& cfg,
                         const std::unordered_map<const IRValue*, AllocaInfo*>& info_by_alloc,
                         std::unordered_set<const IRValue*>& invalid_allocs)
{
    for (auto* block : function.basic_blocks_) {
        if (block == nullptr) {
            continue;
        }

        for (auto* inst : block->insts_) {
            if (inst == nullptr) {
                continue;
            }

            std::unordered_map<const IRValue*, size_t> operand_counts;
            ir_rewrite::for_each_operand(*inst, [&](IRValue*& operand) {
                if (info_by_alloc.find(operand) != info_by_alloc.end()) {
                    ++operand_counts[operand];
                }
            });

            for (const auto& [alloc_value, count] : operand_counts) {
                size_t allowed_count = 0;
                if (inst->kind_ == IRValueKind::IR_LOAD
                    && inst->as<IRLoadInst>()->src_ == alloc_value) {
                    ++allowed_count;
                }
                if (inst->kind_ == IRValueKind::IR_STORE
                    && inst->as<IRStoreInst>()->dest_ == alloc_value) {
                    ++allowed_count;
                }

                if (count != allowed_count || !cfg.is_reachable(*block)) {
                    invalid_allocs.insert(alloc_value);
                }
            }

            if (inst->kind_ == IRValueKind::IR_LOAD) {
                auto* load = inst->as<IRLoadInst>();
                auto* alloc = as_candidate_alloc(load->src_, info_by_alloc);
                if (alloc == nullptr) {
                    continue;
                }

                info_by_alloc.at(alloc)->loads.push_back(load);
            }

            if (inst->kind_ == IRValueKind::IR_STORE) {
                auto* store = inst->as<IRStoreInst>();
                auto* alloc = as_candidate_alloc(store->dest_, info_by_alloc);
                if (alloc == nullptr) {
                    continue;
                }

                if (store->value_ == nullptr
                    || store->value_->type_ == nullptr
                    || !store->value_->type_->is_int32()) {
                    invalid_allocs.insert(alloc);
                    continue;
                }

                auto* info = info_by_alloc.at(alloc);
                info->stores.push_back(store);
                info->def_blocks.insert(block);
            }
        }
    }
}

void place_block_arguments(AllocaInfo& info, const DominanceAnalysis& dominance)
{
    if (info.loads.empty()) {
        return;
    }

    std::vector<const IRBasicBlock*> worklist(info.def_blocks.begin(), info.def_blocks.end());
    std::unordered_set<const IRBasicBlock*> in_worklist(info.def_blocks.begin(),
                                                       info.def_blocks.end());

    while (!worklist.empty()) {
        const auto* block = worklist.back();
        worklist.pop_back();

        for (const auto* frontier_block : dominance.dominance_frontier(*block)) {
            if (!info.phi_blocks.insert(frontier_block).second) {
                continue;
            }

            if (info.def_blocks.find(frontier_block) == info.def_blocks.end()
                && in_worklist.insert(frontier_block).second) {
                worklist.push_back(frontier_block);
            }
        }
    }
}

bool has_value_on_all_required_paths(const AllocaInfo& info,
                                     const DominanceAnalysis& dominance,
                                     const IRBasicBlock& block,
                                     bool has_value)
{
    if (info.phi_blocks.find(&block) != info.phi_blocks.end()) {
        has_value = true;
    }

    for (const auto* inst : block.insts_) {
        if (inst == nullptr) {
            continue;
        }

        if (inst->kind_ == IRValueKind::IR_LOAD
            && inst->as<IRLoadInst>()->src_ == info.alloc
            && !has_value) {
            return false;
        }

        if (inst->kind_ == IRValueKind::IR_STORE
            && inst->as<IRStoreInst>()->dest_ == info.alloc) {
            has_value = true;
        }

        if (inst->kind_ == IRValueKind::IR_JUMP) {
            const auto* jump = inst->as<IRJumpInst>();
            if (jump->jump_basic_block_ != nullptr
                && info.phi_blocks.find(jump->jump_basic_block_) != info.phi_blocks.end()
                && !has_value) {
                return false;
            }
        }

        if (inst->kind_ == IRValueKind::IR_BRANCH) {
            const auto* branch = inst->as<IRBranchInst>();
            if (branch->if_basic_block_ != nullptr
                && info.phi_blocks.find(branch->if_basic_block_) != info.phi_blocks.end()
                && !has_value) {
                return false;
            }
            if (branch->else_basic_block_ != nullptr
                && info.phi_blocks.find(branch->else_basic_block_) != info.phi_blocks.end()
                && !has_value) {
                return false;
            }
        }
    }

    for (const auto* child : dominance.dominator_tree_children(block)) {
        if (child == nullptr
            || !has_value_on_all_required_paths(info, dominance, *child, has_value)) {
            return false;
        }
    }

    return true;
}

bool can_promote_without_undef(const AllocaInfo& info,
                               const CFGAnalysis& cfg,
                               const DominanceAnalysis& dominance)
{
    const auto* entry = cfg.entry();
    if (entry == nullptr) {
        return false;
    }

    return has_value_on_all_required_paths(info, dominance, *entry, false);
}

void create_block_params(IRModule& module,
                         const std::vector<AllocaInfo*>& allocas,
                         std::unordered_map<const IRValue*, AllocaInfo*>& info_by_block_param)
{
    size_t name_index = 0;
    for (auto* info : allocas) {
        for (const auto* block : info->phi_blocks) {
            auto* mutable_block = const_cast<IRBasicBlock*>(block);
            auto* param = module.make_block_param(
                *mutable_block,
                info->alloc->type_->as<IRPointerType>()->base_type,
                make_block_param_name(*info->alloc, name_index++));
            info->block_params[block] = param;
            info_by_block_param[param] = info;
        }
    }
}

void append_mem2reg_edge_args(IRBasicBlock* target,
                              std::vector<IRValue*>& args,
                              const std::unordered_map<IRAllocInst*, IRValue*>& current_values,
                              const std::unordered_map<const IRValue*, AllocaInfo*>& info_by_block_param)
{
    if (target == nullptr) {
        return;
    }

    for (auto* param : target->params_) {
        const auto param_info_iter = info_by_block_param.find(param);
        if (param_info_iter == info_by_block_param.end()) {
            continue;
        }

        auto* alloc = param_info_iter->second->alloc;
        const auto current_iter = current_values.find(alloc);
        if (current_iter != current_values.end() && current_iter->second != nullptr) {
            args.push_back(current_iter->second);
        }
    }
}

void append_successor_args(IRValue& terminator,
                           const std::unordered_map<IRAllocInst*, IRValue*>& current_values,
                           const std::unordered_map<const IRValue*, AllocaInfo*>& info_by_block_param)
{
    switch (terminator.kind_) {
    case IRValueKind::IR_BRANCH: {
        auto* branch = terminator.as<IRBranchInst>();
        append_mem2reg_edge_args(branch->if_basic_block_,
                                 branch->if_args_,
                                 current_values,
                                 info_by_block_param);
        append_mem2reg_edge_args(branch->else_basic_block_,
                                 branch->else_args_,
                                 current_values,
                                 info_by_block_param);
        break;
    }
    case IRValueKind::IR_JUMP: {
        auto* jump = terminator.as<IRJumpInst>();
        append_mem2reg_edge_args(jump->jump_basic_block_,
                                 jump->args_,
                                 current_values,
                                 info_by_block_param);
        break;
    }
    default:
        break;
    }
}

void rename_block(PromotionState& state,
                  IRBasicBlock& block,
                  std::unordered_map<IRAllocInst*, IRValue*> current_values)
{
    for (const auto* param : block.params_) {
        const auto iter = state.info_by_block_param.find(param);
        if (iter != state.info_by_block_param.end()) {
            current_values[iter->second->alloc] = const_cast<IRValue*>(param);
        }
    }

    const auto original_insts = block.insts_;
    for (auto* inst : original_insts) {
        if (inst == nullptr) {
            continue;
        }

        const auto info_iter = state.info_by_alloc.find(inst);
        if (info_iter != state.info_by_alloc.end()) {
            ir_rewrite::erase_instruction(block, *inst);
            continue;
        }

        if (inst->kind_ == IRValueKind::IR_LOAD) {
            auto* load = inst->as<IRLoadInst>();
            const auto load_info_iter = state.info_by_alloc.find(load->src_);
            if (load_info_iter != state.info_by_alloc.end()) {
                auto* replacement = current_values[load_info_iter->second->alloc];
                ir_rewrite::replace_all_uses(state.function, load, replacement);
                ir_rewrite::erase_instruction(block, *load);
                continue;
            }
        }

        if (inst->kind_ == IRValueKind::IR_STORE) {
            auto* store = inst->as<IRStoreInst>();
            const auto store_info_iter = state.info_by_alloc.find(store->dest_);
            if (store_info_iter != state.info_by_alloc.end()) {
                current_values[store_info_iter->second->alloc] = store->value_;
                ir_rewrite::erase_instruction(block, *store);
                continue;
            }
        }
    }

    if (!block.insts_.empty()) {
        append_successor_args(*block.insts_.back(),
                              current_values,
                              state.info_by_block_param);
    }

    for (const auto* child : state.dominance.dominator_tree_children(block)) {
        if (child != nullptr) {
            rename_block(state, *const_cast<IRBasicBlock*>(child), current_values);
        }
    }
}

} // namespace

std::string_view Mem2RegPass::name() const
{
    return "mem2reg";
}

bool Mem2RegPass::run(IRModule& module)
{
    bool changed = false;

    for (auto* function : module.funcs_) {
        if (function == nullptr || function->is_declaration_) {
            continue;
        }

        changed = run_function(module, *function) || changed;
    }

    return changed;
}

bool Mem2RegPass::run_function(IRModule& module, IRFunction& function)
{
    CFGAnalysis cfg(function);
    DominanceAnalysis dominance(cfg);

    std::unordered_map<const IRValue*, AllocaInfo*> info_by_alloc;
    auto infos = collect_alloca_infos(function, cfg, info_by_alloc);
    if (infos.empty()) {
        return false;
    }

    std::unordered_set<const IRValue*> invalid_allocs;
    collect_direct_uses(function, cfg, info_by_alloc, invalid_allocs);

    std::vector<AllocaInfo*> promotable_allocas;
    for (auto& info : infos) {
        if (invalid_allocs.find(info->alloc) != invalid_allocs.end()) {
            continue;
        }

        place_block_arguments(*info, dominance);
        if (!can_promote_without_undef(*info, cfg, dominance)) {
            continue;
        }

        promotable_allocas.push_back(info.get());
    }

    if (promotable_allocas.empty()) {
        return false;
    }

    std::unordered_map<const IRValue*, AllocaInfo*> info_by_block_param;
    create_block_params(module, promotable_allocas, info_by_block_param);

    std::unordered_map<const IRValue*, AllocaInfo*> promotable_by_alloc;
    for (auto* info : promotable_allocas) {
        promotable_by_alloc[info->alloc] = info;
    }

    PromotionState state{
        function,
        dominance,
        promotable_allocas,
        std::move(promotable_by_alloc),
        std::move(info_by_block_param),
    };

    auto* entry = const_cast<IRBasicBlock*>(cfg.entry());
    if (entry == nullptr) {
        return false;
    }

    rename_block(state, *entry, {});
    return true;
}

} // namespace rewind_ir
