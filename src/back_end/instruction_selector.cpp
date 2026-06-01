#include "instruction_selector.h"

#include "ir_type.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace riscv
{

namespace
{

Register pick_scratch(Register avoid1, Register avoid2 = Register::x0)
{
    for (Register reg : {Register::t0, Register::t1, Register::t2}) {
        if (reg != avoid1 && reg != avoid2) {
            return reg;
        }
    }
    throw std::runtime_error("no available scratch register");
}

const rewind_ir::IRArrayType* get_array_storage_type(const rewind_ir::IRValue* value)
{
    if (value == nullptr || value->type_ == nullptr || !value->type_->is_pointer()) {
        return nullptr;
    }

    const auto* pointer_base_type =
        value->type_->as<rewind_ir::IRPointerType>()->base_type;
    if (pointer_base_type == nullptr || !pointer_base_type->is_array()) {
        return nullptr;
    }

    return pointer_base_type->as<rewind_ir::IRArrayType>();
}

} // namespace

MachineFunction InstructionSelector::select_function(const rewind_ir::IRFunction& func)
{
    current_function_ = &func;
    frame_.build(func);

    MachineFunction machine_function(sanitize_symbol(func.name_));
    machine_function.frame.set_frame_size(frame_.frame_size());
    if (frame_.has_saved_ra()) {
        machine_function.frame.set_saved_ra(frame_.ra_offset());
    }

    current_machine_function_ = &machine_function;

    for (const auto* block : func.basic_blocks_) {
        select_basic_block(*block);
    }

    current_machine_block_ = nullptr;
    current_machine_function_ = nullptr;
    current_ir_block_ = nullptr;
    current_function_ = nullptr;
    return machine_function;
}

bool InstructionSelector::fits_i12(int32_t value)
{
    return value >= -2048 && value <= 2047;
}

std::string InstructionSelector::sanitize_symbol(std::string_view name)
{
    if (!name.empty() && (name.front() == '@' || name.front() == '%')) {
        name.remove_prefix(1);
    }

    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '_' || ch == '.') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }

    if (out.empty()) {
        out = "_anon";
    }
    return out;
}

std::string InstructionSelector::basic_block_label(const rewind_ir::IRBasicBlock& block) const
{
    return ".L" + sanitize_symbol(current_function_->name_) + "_"
           + sanitize_symbol(block.name_);
}

std::string InstructionSelector::edge_label(const rewind_ir::IRBasicBlock& from,
                                            const rewind_ir::IRBasicBlock& to,
                                            std::string_view suffix) const
{
    return basic_block_label(from) + "_to_" + sanitize_symbol(to.name_) + "_"
           + std::string(suffix);
}

void InstructionSelector::emit(MachineInstr instr)
{
    if (current_machine_block_ == nullptr) {
        throw std::runtime_error("no current machine basic block");
    }
    current_machine_block_->add_instr(std::move(instr));
}

void InstructionSelector::select_basic_block(const rewind_ir::IRBasicBlock& block)
{
    current_machine_block_ = &current_machine_function_->add_block(basic_block_label(block));
    current_ir_block_ = &block;

    if (current_function_->basic_blocks_.front() == &block) {
        select_prologue();
    }

    for (const auto* inst : block.insts_) {
        select_instruction(*inst);
    }
}

void InstructionSelector::select_instruction(const rewind_ir::IRValue& inst)
{
    switch (inst.kind_) {
    case rewind_ir::IRValueKind::IR_ALLOC:
        select_alloc(*inst.as<rewind_ir::IRAllocInst>());
        return;
    case rewind_ir::IRValueKind::IR_STORE:
        select_store(*inst.as<rewind_ir::IRStoreInst>());
        return;
    case rewind_ir::IRValueKind::IR_LOAD:
        select_load(*inst.as<rewind_ir::IRLoadInst>());
        return;
    case rewind_ir::IRValueKind::IR_BINARY:
        select_binary(*inst.as<rewind_ir::IRBinaryInst>());
        return;
    case rewind_ir::IRValueKind::IR_CALL:
        select_call(*inst.as<rewind_ir::IRCallInst>());
        return;
    case rewind_ir::IRValueKind::IR_RETURN:
        select_return(*inst.as<rewind_ir::IRReturnInst>());
        return;
    case rewind_ir::IRValueKind::IR_JUMP:
        select_jump(*inst.as<rewind_ir::IRJumpInst>());
        return;
    case rewind_ir::IRValueKind::IR_BRANCH:
        select_branch(*inst.as<rewind_ir::IRBranchInst>());
        return;
    case rewind_ir::IRValueKind::IR_GET_PTR:
        select_get_ptr(*inst.as<rewind_ir::IRGetPtrInst>());
        return;
    case rewind_ir::IRValueKind::IR_GET_ELEM_PTR:
        select_get_elem_ptr(*inst.as<rewind_ir::IRGetElemPtrInst>());
        return;
    default:
        break;
    }

    throw std::runtime_error("unsupported rewind IR instruction in instruction selector: "
                             + inst.name_);
}

void InstructionSelector::select_alloc(const rewind_ir::IRAllocInst&)
{
}

void InstructionSelector::select_store(const rewind_ir::IRStoreInst& inst)
{
    if (inst.value_->is_aggregate() || inst.value_->is_zero_init()) {
        const auto* dest_type =
            inst.dest_->type_->as<rewind_ir::IRPointerType>()->base_type;
        materialize_pointer(inst.dest_, Register::t2);
        select_initializer_store(inst.value_, dest_type, Register::t2, 0);
        return;
    }

    materialize_value(inst.value_, Register::t0);
    store_to_addressable(inst.dest_, Register::t0);
}

void InstructionSelector::select_initializer_store(const rewind_ir::IRValue* init,
                                                   const rewind_ir::IRType* type,
                                                   Register base,
                                                   int32_t offset)
{
    if (type->is_int32()) {
        if (init->is_zero_init()) {
            if (fits_i12(offset)) {
                emit(MachineInstr::make_sw(Register::x0, base, offset));
            } else {
                emit(MachineInstr::make_li(Register::t1, offset));
                emit(MachineInstr::make_add(Register::t1, base, Register::t1));
                emit(MachineInstr::make_sw(Register::x0, Register::t1, 0));
            }
            return;
        }

        materialize_value(init, Register::t0);
        if (fits_i12(offset)) {
            emit(MachineInstr::make_sw(Register::t0, base, offset));
        } else {
            emit(MachineInstr::make_li(Register::t1, offset));
            emit(MachineInstr::make_add(Register::t1, base, Register::t1));
            emit(MachineInstr::make_sw(Register::t0, Register::t1, 0));
        }
        return;
    }

    const auto* array_type = type->as<rewind_ir::IRArrayType>();
    const auto elem_size = data_layout_.type_size(array_type->element_type);

    if (init->is_zero_init()) {
        for (size_t i = 0; i < array_type->length; ++i) {
            select_initializer_store(
                init,
                array_type->element_type,
                base,
                offset + static_cast<int32_t>(i) * elem_size);
        }
        return;
    }

    const auto* aggregate = init->as<rewind_ir::IRAggregate>();
    for (size_t i = 0; i < aggregate->elems_.size(); ++i) {
        select_initializer_store(
            aggregate->elems_[i],
            array_type->element_type,
            base,
            offset + static_cast<int32_t>(i) * elem_size);
    }
}

void InstructionSelector::select_load(const rewind_ir::IRLoadInst& inst)
{
    materialize_pointer(inst.src_, Register::t0);
    emit(MachineInstr::make_lw(Register::t0, Register::t0, 0));
    spill_value(&inst, Register::t0);
}

void InstructionSelector::select_binary(const rewind_ir::IRBinaryInst& inst)
{
    materialize_value(inst.lhs_, Register::t0);
    materialize_value(inst.rhs_, Register::t1);

    switch (inst.op_) {
    case rewind_ir::IRBinaryOp::ADD:
        emit(MachineInstr::make_add(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::SUB:
        emit(MachineInstr::make_sub(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::MUL:
        emit(MachineInstr::make_mul(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::DIV:
        emit(MachineInstr::make_div(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::MOD:
        emit(MachineInstr::make_rem(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::AND:
        emit(MachineInstr::make_and(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::OR:
        emit(MachineInstr::make_or(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::XOR:
        emit(MachineInstr::make_xor(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::EQ:
        emit(MachineInstr::make_xor(Register::t0, Register::t0, Register::t1));
        emit(MachineInstr::make_seqz(Register::t0, Register::t0));
        break;
    case rewind_ir::IRBinaryOp::NEQ:
        emit(MachineInstr::make_xor(Register::t0, Register::t0, Register::t1));
        emit(MachineInstr::make_snez(Register::t0, Register::t0));
        break;
    case rewind_ir::IRBinaryOp::LT:
        emit(MachineInstr::make_slt(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::GT:
        emit(MachineInstr::make_slt(Register::t0, Register::t1, Register::t0));
        break;
    case rewind_ir::IRBinaryOp::LE:
        emit(MachineInstr::make_slt(Register::t0, Register::t1, Register::t0));
        emit(MachineInstr::make_seqz(Register::t0, Register::t0));
        break;
    case rewind_ir::IRBinaryOp::GE:
        emit(MachineInstr::make_slt(Register::t0, Register::t0, Register::t1));
        emit(MachineInstr::make_seqz(Register::t0, Register::t0));
        break;
    case rewind_ir::IRBinaryOp::SHL:
        emit(MachineInstr::make_sll(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::SHR:
        emit(MachineInstr::make_srl(Register::t0, Register::t0, Register::t1));
        break;
    case rewind_ir::IRBinaryOp::SAR:
        emit(MachineInstr::make_sra(Register::t0, Register::t0, Register::t1));
        break;
    }

    spill_value(&inst, Register::t0);
}

void InstructionSelector::select_call(const rewind_ir::IRCallInst& inst)
{
    const auto register_arg_count =
        std::min(inst.args_.size(), CallingConvention::ArgRegisterCount);

    for (size_t i = 0; i < register_arg_count; ++i) {
        materialize_value(inst.args_[i], calling_convention_.arg_register(i));
    }

    for (size_t i = CallingConvention::ArgRegisterCount; i < inst.args_.size(); ++i) {
        materialize_value(inst.args_[i], Register::t0);
        select_stack_store(Register::t0, frame_.outgoing_arg_offset(i));
    }

    emit(MachineInstr::make_call(sanitize_symbol(inst.callee_->name_)));

    if (!inst.type_->is_unit()) {
        spill_value(&inst, Register::a0);
    }
}

void InstructionSelector::select_branch(const rewind_ir::IRBranchInst& inst)
{
    materialize_value(inst.cond_, Register::t0);

    const auto if_label = basic_block_label(*inst.if_basic_block_);
    const auto else_label = basic_block_label(*inst.else_basic_block_);

    if (inst.if_args_.empty() && inst.else_args_.empty()) {
        emit(MachineInstr::make_bnez(Register::t0, if_label));
        emit(MachineInstr::make_j(else_label));
        return;
    }

    const auto true_edge = edge_label(
        *current_ir_block_,
        *inst.if_basic_block_,
        "true");
    emit(MachineInstr::make_bnez(Register::t0, true_edge));
    select_edge_args(*inst.else_basic_block_, inst.else_args_);
    emit(MachineInstr::make_j(else_label));
    emit(MachineInstr::make_label(true_edge));
    select_edge_args(*inst.if_basic_block_, inst.if_args_);
    emit(MachineInstr::make_j(if_label));
}

void InstructionSelector::select_jump(const rewind_ir::IRJumpInst& inst)
{
    select_edge_args(*inst.jump_basic_block_, inst.args_);
    emit(MachineInstr::make_j(basic_block_label(*inst.jump_basic_block_)));
}

void InstructionSelector::select_return(const rewind_ir::IRReturnInst& inst)
{
    if (inst.dst_ != nullptr) {
        materialize_value(inst.dst_, Register::a0);
    }
    select_epilogue();
    emit(MachineInstr::make_ret());
}

void InstructionSelector::select_get_ptr(const rewind_ir::IRGetPtrInst& inst)
{
    if (inst.src_ == nullptr || inst.src_->type_ == nullptr || !inst.src_->type_->is_pointer()) {
        throw std::runtime_error("getptr source is not pointer value");
    }

    const auto* pointee_type = inst.src_->type_->as<rewind_ir::IRPointerType>()->base_type;
    materialize_value(inst.src_, Register::t0);
    materialize_value(inst.index_, Register::t1);

    const auto elem_size = data_layout_.type_size(pointee_type);
    if (elem_size != 1) {
        emit(MachineInstr::make_li(Register::t2, elem_size));
        emit(MachineInstr::make_mul(Register::t1, Register::t1, Register::t2));
    }

    emit(MachineInstr::make_add(Register::t0, Register::t0, Register::t1));
    spill_value(&inst, Register::t0);
}

void InstructionSelector::select_get_elem_ptr(const rewind_ir::IRGetElemPtrInst& inst)
{
    const auto* array_type = get_array_storage_type(inst.src_);
    if (array_type == nullptr) {
        throw std::runtime_error("getelemptr source is not array storage");
    }

    materialize_pointer(inst.src_, Register::t0);
    materialize_value(inst.index_, Register::t1);

    const auto elem_size = data_layout_.type_size(array_type->element_type);
    if (elem_size != 1) {
        emit(MachineInstr::make_li(Register::t2, elem_size));
        emit(MachineInstr::make_mul(Register::t1, Register::t1, Register::t2));
    }

    emit(MachineInstr::make_add(Register::t0, Register::t0, Register::t1));
    spill_value(&inst, Register::t0);
}

void InstructionSelector::materialize_value(const rewind_ir::IRValue* value, Register dst)
{
    if (value == nullptr) {
        throw std::runtime_error("materialize_value received nullptr");
    }

    if (value->is_integer()) {
        emit(MachineInstr::make_li(dst, value->as<rewind_ir::IRConstant>()->value_));
        return;
    }

    if (value->is_func_arg_ref()) {
        const auto* func_arg_ref = value->as<rewind_ir::IRFuncArgRef>();
        const size_t param_index = static_cast<size_t>(func_arg_ref->index_);
        if (calling_convention_.is_register_arg(param_index)) {
            emit(MachineInstr::make_mv(dst, calling_convention_.arg_register(param_index)));
        } else {
            select_stack_load(dst, frame_.incoming_stack_arg_offset(param_index));
        }
        return;
    }

    if (value->is_global_alloc()) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        emit(MachineInstr::make_la(dst, sanitize_symbol(global_alloc->name_)));
        emit(MachineInstr::make_lw(dst, dst, 0));
        return;
    }

    if (frame_.has_value_slot(value)) {
        select_stack_load(dst, frame_.value_slot(value));
        return;
    }

    if (frame_.has_object_slot(value)) {
        select_stack_load(dst, frame_.object_slot(value));
        return;
    }

    throw std::runtime_error("cannot materialize IR value: " + value->name_);
}

void InstructionSelector::materialize_pointer(const rewind_ir::IRValue* value, Register dst)
{
    if (value == nullptr) {
        throw std::runtime_error("materialize_pointer received nullptr");
    }

    if (value->kind_ == rewind_ir::IRValueKind::IR_GLOBALALLOC) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        emit(MachineInstr::make_la(dst, sanitize_symbol(global_alloc->name_)));
        return;
    }

    if (frame_.has_object_slot(value)) {
        select_stack_address(dst, frame_.object_slot(value));
        return;
    }

    if (frame_.has_value_slot(value)) {
        select_stack_load(dst, frame_.value_slot(value));
        return;
    }

    throw std::runtime_error("cannot materialize IR pointer: " + value->name_);
}

void InstructionSelector::store_to_addressable(const rewind_ir::IRValue* value, Register src)
{
    if (frame_.has_object_slot(value)) {
        select_stack_store(src, frame_.object_slot(value));
        return;
    }

    if (value->is_global_alloc()) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        emit(MachineInstr::make_la(Register::t1, sanitize_symbol(global_alloc->name_)));
        emit(MachineInstr::make_sw(src, Register::t1, 0));
        return;
    }

    if (frame_.has_value_slot(value)) {
        materialize_pointer(value, Register::t1);
        emit(MachineInstr::make_sw(src, Register::t1, 0));
        return;
    }

    throw std::runtime_error(value->name_ + " is not addressable");
}

void InstructionSelector::spill_value(const rewind_ir::IRValue* value, Register src)
{
    if (!frame_.has_value_slot(value)) {
        throw std::runtime_error("missing spill slot for IR value: " + value->name_);
    }
    select_stack_store(src, frame_.value_slot(value));
}

void InstructionSelector::select_edge_args(const rewind_ir::IRBasicBlock& target,
                                           const std::vector<rewind_ir::IRValue*>& args)
{
    if (args.empty()) {
        return;
    }

    if (args.size() != target.params_.size()) {
        throw std::runtime_error("machine edge argument count mismatch");
    }

    for (size_t i = 0; i < args.size(); ++i) {
        materialize_value(args[i], Register::t0);
        spill_value(target.params_[i], Register::t0);
    }
}

void InstructionSelector::select_prologue()
{
    select_adjust_sp(-frame_.frame_size());
    if (frame_.has_saved_ra()) {
        select_stack_store(Register::ra, frame_.ra_offset());
    }
}

void InstructionSelector::select_epilogue()
{
    if (frame_.has_saved_ra()) {
        select_stack_load(Register::ra, frame_.ra_offset());
    }
    select_adjust_sp(frame_.frame_size());
}

void InstructionSelector::select_adjust_sp(int32_t delta)
{
    if (delta == 0) {
        return;
    }

    if (fits_i12(delta)) {
        emit(MachineInstr::make_addi(Register::sp, Register::sp, delta));
        return;
    }

    emit(MachineInstr::make_li(Register::t0, delta));
    emit(MachineInstr::make_add(Register::sp, Register::sp, Register::t0));
}

void InstructionSelector::select_stack_address(Register rd, int32_t offset)
{
    if (fits_i12(offset)) {
        emit(MachineInstr::make_addi(rd, Register::sp, offset));
        return;
    }

    emit(MachineInstr::make_li(rd, offset));
    emit(MachineInstr::make_add(rd, Register::sp, rd));
}

void InstructionSelector::select_stack_load(Register rd, int32_t offset)
{
    if (fits_i12(offset)) {
        emit(MachineInstr::make_lw(rd, Register::sp, offset));
        return;
    }

    select_stack_address(rd, offset);
    emit(MachineInstr::make_lw(rd, rd, 0));
}

void InstructionSelector::select_stack_store(Register rs, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit(MachineInstr::make_sw(rs, Register::sp, offset));
        return;
    }

    Register addr_reg = scratch;
    if (addr_reg == rs) {
        addr_reg = pick_scratch(rs);
    }

    select_stack_address(addr_reg, offset);
    emit(MachineInstr::make_sw(rs, addr_reg, 0));
}

} // namespace riscv
