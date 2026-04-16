#include <cstdint>
#include <iostream>
#include "riscv.h"
#include "ir_type.h"
#include "rewind_ir.h"
#include <cctype>
#include <ostream>
#include <stdexcept>

namespace riscv
{

namespace
{

constexpr int32_t kWordSize = 4;
constexpr int32_t align = 16;
constexpr size_t kArgRegisterCount = 8;

const int32_t* find_slot(const std::unordered_map<const rewind_ir::IRValue*, int32_t>& slots,
                         const rewind_ir::IRValue* value)
{
    const auto it = slots.find(value);
    if (it == slots.end()) {
        return nullptr;
    }
    return &it->second;
}

// return the base type of pointer
const rewind_ir::IRType* storage_base_type(const rewind_ir::IRValue* value)
{
    if (value == nullptr || value->type_ == nullptr || !value->type_->is_pointer()) {
        return nullptr;
    }
    return value->type_->as<rewind_ir::IRPointerType>()->base_type;
}

// return array type
const rewind_ir::IRArrayType* storage_array_type(const rewind_ir::IRValue* value)
{
    const auto* base_type = storage_base_type(value);
    if (base_type == nullptr || !base_type->is_array()) {
        return nullptr;
    }
    return base_type->as<rewind_ir::IRArrayType>();
}

} // namespace

// todo : directly cal type size
// ? further adjustments are needed
bool FunctionFrame::produces_stack_value(const rewind_ir::IRValue& value)
{
    switch (value.kind_) {
    case rewind_ir::IRValueKind::IR_BINARY:
    case rewind_ir::IRValueKind::IR_LOAD:
    case rewind_ir::IRValueKind::IR_GET_PTR:
    case rewind_ir::IRValueKind::IR_GET_ELEM_PTR:
        return true;
    case rewind_ir::IRValueKind::IR_CALL:
        return value.type_ != nullptr && !value.type_->is_unit();
    default:
        return false;
    }
}

int32_t FunctionFrame::alloc_size(const rewind_ir::IRAllocInst& inst)
{
    if (inst.type_ == nullptr || !inst.type_->is_pointer()) {
        return kWordSize;
    }

    const auto* pointer_type = inst.type_->as<rewind_ir::IRPointerType>();
    auto size = rewind_ir::IRTypeContext::instance().getTypeSize(pointer_type->base_type);
    return static_cast<int32_t>(size == 0 ? kWordSize : size);
}

// align must be power of 2
int32_t FunctionFrame::align_to(int32_t value, int32_t align)
{
    return (value + align - 1) & ~(align - 1);
}

/*
 | previous function stack frame |     high address
 | ----------------------------- |
 |       return address          |
 |       local variables         |
 |       temp values             | <-- inst result
 |       function params         |
 |       (tenth param)           |
 |       (ninth param)           |
 | ----------------------------- | <-- sp register address
 |                               |
 |                               |     low address
 * the design of stack frame
 * 1. low address part stores outgoing call arguments beyond a0-a7
 * 2. then stores local variable object slots and IR median result slots
 * 3. if this function contains a call, save ra at sp + frame_size - kWordSize
 * 4. align stack frame size to 16 bytes
 */
void FunctionFrame::build(const rewind_ir::IRFunction& func)
{
    object_slots_.clear();
    value_slots_.clear();
    next_slot_offset_ = 0;
    frame_size_ = 0;
    ra_offset_ = 0;
    outgoing_arg_size_ = 0;
    has_saved_ra_ = false;
    size_t max_func_param_size = 0;
    int32_t payload_size = 0;

    // first traversal to cal frame stack size
    for (const auto* block : func.basic_blocks_) {
        for (const auto* inst : block->insts_) {
            if (inst->kind_ == rewind_ir::IRValueKind::IR_CALL) {
                const auto* call_inst = inst->as<rewind_ir::IRCallInst>();
                has_saved_ra_ = true;
                max_func_param_size = std::max(max_func_param_size, call_inst->args_.size());
            }

            if (inst->kind_ == rewind_ir::IRValueKind::IR_ALLOC) {
                const auto* alloc = inst->as<rewind_ir::IRAllocInst>();
                payload_size += alloc_size(*alloc);
                continue;
            }

            if (produces_stack_value(*inst)) {
                payload_size += kWordSize;
            }
        }
    }

    const auto saved_ra_size = has_saved_ra_ ? kWordSize : 0;

    if (max_func_param_size > kArgRegisterCount) {
        outgoing_arg_size_ =
            static_cast<int32_t>((max_func_param_size - kArgRegisterCount) * kWordSize);
    }

    frame_size_ = align_to(outgoing_arg_size_ + payload_size + saved_ra_size, align);
    if (has_saved_ra_) {
        ra_offset_ = frame_size_ - kWordSize;
    }

    next_slot_offset_ = outgoing_arg_size_;
    // second traversal to ensure the location distribution of local variable and inst result
    for (const auto* block : func.basic_blocks_) {
        for (const auto* inst : block->insts_) {
            if (inst->kind_ == rewind_ir::IRValueKind::IR_ALLOC) {
                const auto* alloc = inst->as<rewind_ir::IRAllocInst>();
                object_slots_.emplace(inst, next_slot_offset_);
                next_slot_offset_ += alloc_size(*alloc);
                continue;
            }

            if (produces_stack_value(*inst)) {
                value_slots_.emplace(inst, next_slot_offset_);
                next_slot_offset_ += kWordSize;
            }
        }
    }
}

// return the stack frame address of the outgoing arg
int32_t FunctionFrame::outgoing_arg_offset(size_t arg_index) const
{
    if (arg_index < kArgRegisterCount) {
        throw std::runtime_error("outgoing_arg_offset requires stack-passed argument");
    }
    const auto stack_index = arg_index - kArgRegisterCount;
    return static_cast<int32_t>(stack_index * kWordSize);
}

// return the stack frame address of the call function actual arg
int32_t FunctionFrame::incoming_stack_arg_offset(size_t arg_index) const
{
    if (arg_index < kArgRegisterCount) {
        throw std::runtime_error("incoming_stack_arg_offset requires stack-passed argument");
    }
    return frame_size_ + outgoing_arg_offset(arg_index);
}

bool FunctionFrame::has_object_slot(const rewind_ir::IRValue* value) const
{
    return find_slot(object_slots_, value) != nullptr;
}

bool FunctionFrame::has_value_slot(const rewind_ir::IRValue* value) const
{
    return find_slot(value_slots_, value) != nullptr;
}

// return the offest of the variable in stack frame
int32_t FunctionFrame::object_slot(const rewind_ir::IRValue* value) const
{
    if (const auto* slot = find_slot(object_slots_, value)) {
        return *slot;
    }
    throw std::runtime_error("missing object slot");
}

// return the offest of the inst result in stack frame
int32_t FunctionFrame::value_slot(const rewind_ir::IRValue* value) const
{
    if (const auto* slot = find_slot(value_slots_, value)) {
        return *slot;
    }
    throw std::runtime_error("missing value slot");
}

IREmitter::IREmitter(std::ostream& out) : out_(out)
{
}

// imm12 scope [-2048, 2047]
// check value is in scope
bool IREmitter::fits_i12(int32_t value)
{
    return value >= -2048 && value <= 2047;
}

const char* IREmitter::reg_name(Register reg)
{
    switch (reg) {
    case Register::x0:
        return "x0";
    case Register::ra:
        return "ra";
    case Register::sp:
        return "sp";
    case Register::t0:
        return "t0";
    case Register::t1:
        return "t1";
    case Register::t2:
        return "t2";
    case Register::a0:
        return "a0";
    case Register::a1:
        return "a1";
    case Register::a2:
        return "a2";
    case Register::a3:
        return "a3";
    case Register::a4:
        return "a4";
    case Register::a5:
        return "a5";
    case Register::a6:
        return "a6";
    case Register::a7:
        return "a7";
    }
    throw std::runtime_error("unknown register");
}

Register IREmitter::arg_reg(size_t index)
{
    switch (index) {
    case 0:
        return Register::a0;
    case 1:
        return Register::a1;
    case 2:
        return Register::a2;
    case 3:
        return Register::a3;
    case 4:
        return Register::a4;
    case 5:
        return Register::a5;
    case 6:
        return Register::a6;
    case 7:
        return Register::a7;
    default:
        throw std::runtime_error("too many call arguments");
    }
}

/*
 * from @name to name
 * exmple: @abc-1 -> abc_1
 */
std::string IREmitter::sanitize_symbol(std::string_view name)
{
    if (!name.empty() && (name.front() == '@' || name.front() == '%')) {
        name.remove_prefix(1);
    }

    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        // riscv name only allows letters , numbers, '_' and '.'
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

void IREmitter::emit_module(const rewind_ir::IRModule& module)
{
    for (const auto* global_value : module.global_values_) {
        if (!global_value->is_global_alloc()) {
            throw std::runtime_error(global_value->name_ + "not global alloc");
        }
        auto global_alloc = global_value->as<rewind_ir::IRGlobalAllocInst>();
        emit_global_value(*global_alloc);
    }

    for (const auto* func : module.funcs_) {
        if (func->is_declaration_) {
            continue;
        }
        emit_function(*func);
    }
}

void IREmitter::emit_global_value(const rewind_ir::IRGlobalAllocInst& global_alloc)
{
    if (global_alloc.type_ == nullptr || !global_alloc.type_->is_pointer()) {
        throw std::runtime_error("global alloc must have pointer type");
    }

    const std::string& name = sanitize_symbol(global_alloc.name_);

    out_ << "  .data\n";
    out_ << "  .global " << name << "\n";
    out_ << name << ":\n";

    if (global_alloc.init_ == nullptr) {
        throw std::runtime_error("global alloc must have initializer");
    }

    switch (global_alloc.init_->kind_) {
    case rewind_ir::IRValueKind::IR_ZERO_INIT: {
        auto* init = global_alloc.init_->as<rewind_ir::IRZeroInit>();
        if (init->type_->is_int32()) {
            out_ << "  .zero 4" << "\n";
        } else if (init->type_->is_array()) {
            {
                int32_t len = init->type_->as<rewind_ir::IRArrayType>()->length;
                for (int32_t i = 0; i < len; i++) {
                    out_ << "  .zero 4" << "\n";
                }
            }
        } else {
            throw std::runtime_error("unsupported global init type");
        }
        break;
    }
    case rewind_ir::IRValueKind::IR_INTEGER: {
        const auto* init = global_alloc.init_->as<rewind_ir::IRConstant>();
        out_ << " .word " << init->value_ << "\n";
        break;
    }
    case rewind_ir::IRValueKind::IR_AGGREGATE: {
        const auto* init = global_alloc.init_->as<rewind_ir::IRAggregate>();
        for (const auto* item : init->elems_) {
            if (item->is_integer()) {
                const auto* constant = item->as<rewind_ir::IRConstant>();
                out_ << "  .word " << constant->value_ << "\n";
            } else {
                throw std::runtime_error("global array elem is not constant");
            }
        }
        break;
    }
    default:
        throw std::runtime_error("not supported other global initialization kind");
    }

    out_ << "\n";
}

void IREmitter::emit_function(const rewind_ir::IRFunction& func)
{
    out_ << "  .text\n";
    out_ << "  .globl " << sanitize_symbol(func.name_) << "\n";

    // cal stack frame size
    current_function_ = &func;
    frame_.build(func);

    out_ << sanitize_symbol(func.name_) << ":\n";
    emit_prologue();

    for (const auto* block : func.basic_blocks_) {
        emit_basic_block(*block);
    }

    out_ << "\n";
}

std::string IREmitter::basic_block_label(const rewind_ir::IRBasicBlock& block) const
{
    if (current_function_ == nullptr) {
        throw std::runtime_error("basic block label requested without current function");
    }

    return ".L" + sanitize_symbol(current_function_->name_) + "_"
           + sanitize_symbol(block.name_);
}

void IREmitter::emit_basic_block(const rewind_ir::IRBasicBlock& block)
{
    out_ << basic_block_label(block) << ":\n";

    for (const auto* inst : block.insts_) {
        emit_instruction(*inst);
    }
}

void IREmitter::emit_instruction(const rewind_ir::IRValue& inst)
{
    switch (inst.kind_) {
    case rewind_ir::IRValueKind::IR_ALLOC:
        emit_alloc(*inst.as<rewind_ir::IRAllocInst>());
        return;
    case rewind_ir::IRValueKind::IR_STORE:
        emit_store(*inst.as<rewind_ir::IRStoreInst>());
        return;
    case rewind_ir::IRValueKind::IR_LOAD:
        emit_load(*inst.as<rewind_ir::IRLoadInst>());
        return;
    case rewind_ir::IRValueKind::IR_BINARY:
        emit_binary(*inst.as<rewind_ir::IRBinaryInst>());
        return;
    case rewind_ir::IRValueKind::IR_CALL:
        emit_call(*inst.as<rewind_ir::IRCallInst>());
        return;
    case rewind_ir::IRValueKind::IR_RETURN:
        emit_return(*inst.as<rewind_ir::IRReturnInst>());
        return;
    case rewind_ir::IRValueKind::IR_INTEGER:
        return;
    case rewind_ir::IRValueKind::IR_JUMP:
        emit_jump(*inst.as<rewind_ir::IRJumpInst>());
        return;
    case rewind_ir::IRValueKind::IR_BRANCH:
        emit_branch(*inst.as<rewind_ir::IRBranchInst>());
        return;
    case rewind_ir::IRValueKind::IR_GET_ELEM_PTR:
        emit_get_elem_ptr(*inst.as<rewind_ir::IRGetElemPtrInst>());
        return;
    default:
        break;
    }

    throw std::runtime_error("unsupported rewind IR instruction in RISC-V backend: " + inst.name_);
}

/*
 * IR instruction lowering
 */
void IREmitter::emit_get_elem_ptr(const rewind_ir::IRGetElemPtrInst& inst)
{
    const auto* array_type = storage_array_type(inst.src_);
    if (array_type == nullptr) {
        throw std::runtime_error("getelemptr source is not array storage");
    }

    materialize_pointer(inst.src_, Register::t0);
    materialize_value(inst.index_, Register::t1);

    const auto elem_size = static_cast<int32_t>(
        rewind_ir::IRTypeContext::instance().getTypeSize(array_type->element_type));
    if (elem_size != 1) {
        emit_li(Register::t2, elem_size);
        emit_mul(Register::t1, Register::t1, Register::t2);
    }

    emit_add(Register::t0, Register::t0, Register::t1);
    spill_value(&inst, Register::t0);
}

void IREmitter::emit_call(const rewind_ir::IRCallInst& inst)
{
    const auto register_arg_count = std::min(inst.args_.size(), kArgRegisterCount);

    // assign registers for args
    for (size_t i = 0; i < register_arg_count; ++i) {
        materialize_value(inst.args_[i], arg_reg(i));
    }

    // assign additional arg to the stack frame
    for (size_t i = kArgRegisterCount; i < inst.args_.size(); ++i) {
        materialize_value(inst.args_[i], Register::t0);
        emit_stack_store(Register::t0, frame_.outgoing_arg_offset(i));
    }

    emit_call_label(sanitize_symbol(inst.callee_->name_));

    if (!inst.type_->is_unit()) {
        spill_value(&inst, Register::a0);
    }
}

// branch inst
void IREmitter::emit_branch(const rewind_ir::IRBranchInst& inst)
{
    if (frame_.has_value_slot(inst.cond_)) {
        emit_stack_load(Register::t0, frame_.value_slot(inst.cond_));
    } else if (inst.cond_->is_integer()) {
        materialize_value(inst.cond_, Register::t0);
    } else {
        throw std::runtime_error("branch condition not found");
    }

    const auto if_label = basic_block_label(*inst.if_basic_block_);
    emit_bnez(Register::t0, if_label);
    const auto else_label = basic_block_label(*inst.else_basic_block_);
    emit_j(else_label);
}

// jump to other inst
void IREmitter::emit_jump(const rewind_ir::IRJumpInst& inst)
{
    const auto label = basic_block_label(*inst.jump_basic_block_);
    emit_j(label);
}

// local alloc will do nothing
void IREmitter::emit_alloc(const rewind_ir::IRAllocInst& inst)
{
    return;
}

// emit store inst
// lw reg stack_frame or li reg imm12
// sw reg stack_frame
void IREmitter::emit_store(const rewind_ir::IRStoreInst& inst)
{
    // store local array
    if (inst.value_->is_aggregate()) {
        const auto* array_type = storage_array_type(inst.dest_);
        if (array_type == nullptr) {
            throw std::runtime_error("aggregate store destination is not array storage");
        }

        const auto* aggregate = inst.value_->as<rewind_ir::IRAggregate>();
        materialize_pointer(inst.dest_, Register::t2);
        // array elem type size
        const auto elem_size = static_cast<int32_t>(
            rewind_ir::IRTypeContext::instance().getTypeSize(array_type->element_type));

        for (size_t i = 0; i < aggregate->elems_.size(); ++i) {
            materialize_value(aggregate->elems_[i], Register::t0);
            const auto offset = static_cast<int32_t>(i) * elem_size;
            if (fits_i12(offset)) {
                emit_sw(Register::t0, Register::t2, offset);
            } else {
                emit_li(Register::t1, offset);
                emit_add(Register::t1, Register::t2, Register::t1);
                emit_sw(Register::t0, Register::t1, 0);
            }
        }
        return;
    }

    // store local variable
    materialize_value(inst.value_, Register::t0);
    store_to_addressable(inst.dest_, Register::t0);
}

void IREmitter::emit_load(const rewind_ir::IRLoadInst& inst)
{
    materialize_pointer(inst.src_, Register::t0);
    emit_lw(Register::t0, Register::t0, 0);

    // store inst result to stack frame(inst)
    spill_value(&inst, Register::t0);
}

void IREmitter::emit_binary(const rewind_ir::IRBinaryInst& inst)
{
    materialize_value(inst.lhs_, Register::t0);
    materialize_value(inst.rhs_, Register::t1);

    switch (inst.op_) {
    case rewind_ir::IRBinaryOp::ADD:
        emit_add(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SUB:
        emit_sub(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::MUL:
        emit_mul(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::DIV:
        emit_div(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::MOD:
        emit_rem(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::AND:
        emit_and(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::OR:
        emit_or(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::XOR:
        emit_xor(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::EQ:
        emit_xor(Register::t0, Register::t0, Register::t1);
        emit_seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::NEQ:
        emit_xor(Register::t0, Register::t0, Register::t1);
        emit_snez(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::LT:
        emit_slt(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::GT:
        emit_slt(Register::t0, Register::t1, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::LE:
        emit_slt(Register::t0, Register::t1, Register::t0);
        emit_seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::GE:
        emit_slt(Register::t0, Register::t0, Register::t1);
        emit_seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::SHL:
        emit_sll(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SHR:
        emit_srl(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SAR:
        emit_sra(Register::t0, Register::t0, Register::t1);
        break;
    }

    spill_value(&inst, Register::t0);
}

// emit return inst
void IREmitter::emit_return(const rewind_ir::IRReturnInst& inst)
{
    if (inst.dst_ != nullptr) {
        materialize_value(inst.dst_, Register::a0);
    }
    emit_epilogue();
    emit_ret();
}

// Operand materialization helpers

// load value to dst register
void IREmitter::materialize_value(const rewind_ir::IRValue* value, Register dst)
{
    // maybe throw runtime error
    if (value == nullptr) {
        throw std::runtime_error("materialize_value received nullptr");
    }

    // immediate
    if (value->kind_ == rewind_ir::IRValueKind::IR_INTEGER) {
        emit_li(dst, value->as<rewind_ir::IRConstant>()->value_);
        return;
    }

    // func arg
    if (value->kind_ == rewind_ir::IRValueKind::FUNC_ARG_REF) {
        const auto* arg = value->as<rewind_ir::IRFuncArgRef>();
        if (arg->index_ < kArgRegisterCount) {
            emit_mv(dst, arg_reg(arg->index_));
        } else {
            emit_stack_load(dst, frame_.incoming_stack_arg_offset(arg->index_));
        }
        return;
    }

    // variable (global variable)
    if (value->kind_ == rewind_ir::IRValueKind::IR_GLOBALALLOC) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        emit_la(dst, sanitize_symbol(global_alloc->name_));
        emit_lw(dst, dst, 0);
        return;
    }

    // inst result
    if (frame_.has_value_slot(value)) {
        emit_stack_load(dst, frame_.value_slot(value));
        return;
    }

    // variable (local variable)
    if (frame_.has_object_slot(value)) {
        emit_stack_load(dst, frame_.object_slot(value));
        return;
    }

    throw std::runtime_error("cannot materialize IR value: " + value->name_);
}

// store dst register to the address of value
void IREmitter::store_to_addressable(const rewind_ir::IRValue* value, Register src)
{
    // local variable
    if (frame_.has_object_slot(value)) {
        emit_stack_store(src, frame_.object_slot(value));
        return;
    }

    // global variable
    if (value->kind_ == rewind_ir::IRValueKind::IR_GLOBALALLOC) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        emit_la(Register::t1, sanitize_symbol(global_alloc->name_));
        emit_sw(src, Register::t1, 0);
        return;
    }

    if (frame_.has_value_slot(value)) {
        materialize_pointer(value, Register::t1);
        emit_sw(src, Register::t1, 0);
        return;
    }

    throw std::runtime_error(value->name_ + " is not addressable");
}

//
void IREmitter::materialize_pointer(const rewind_ir::IRValue* value, Register dst)
{
    if (value == nullptr) {
        throw std::runtime_error("materialize_pointer received nullptr");
    }

    if (value->kind_ == rewind_ir::IRValueKind::IR_GLOBALALLOC) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        emit_la(dst, sanitize_symbol(global_alloc->name_));
        return;
    }

    if (frame_.has_object_slot(value)) {
        emit_stack_address(dst, frame_.object_slot(value));
        return;
    }

    if (frame_.has_value_slot(value)) {
        emit_stack_load(dst, frame_.value_slot(value));
        return;
    }

    throw std::runtime_error("cannot materialize IR pointer: " + value->name_);
}

// store src(inst result) to stack
void IREmitter::spill_value(const rewind_ir::IRValue* value, Register src)
{
    if (!frame_.has_value_slot(value)) {
        throw std::runtime_error("missing spill slot for IR value: " + value->name_);
    }
    emit_stack_store(src, frame_.value_slot(value));
}

// ===== Stack frame helpers =====

// assgin stack frame
void IREmitter::emit_prologue()
{
    emit_adjust_sp(-frame_.frame_size());
    if (frame_.has_saved_ra()) {
        emit_stack_store(Register::ra, frame_.ra_offset());
    }
}

// recover stack frame
void IREmitter::emit_epilogue()
{
    if (frame_.has_saved_ra()) {
        emit_stack_load(Register::ra, frame_.ra_offset());
    }
    emit_adjust_sp(frame_.frame_size());
}

// adjust sp
void IREmitter::emit_adjust_sp(int32_t delta)
{
    // no need to allocate stack frames
    if (delta == 0) {
        return;
    }

    // imm12 range is [-2048, 2047]
    // check if delta out of range
    if (fits_i12(delta)) {
        emit_addi(Register::sp, Register::sp, delta);
        return;
    }

    // load delta to t0 then add sp t0 to sp
    emit_li(Register::t0, delta);
    emit_add(Register::sp, Register::sp, Register::t0);
}

// ===== Stack access helpers =====

// compute stack address and store in rd
void IREmitter::emit_stack_address(Register rd, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit_addi(rd, Register::sp, offset);
        return;
    }

    if (scratch == rd) {
        throw std::runtime_error("stack address scratch register conflicts with destination register");
    }

    emit_li(scratch, offset);
    emit_add(rd, Register::sp, scratch);
}

// load the value of stack address to rd
void IREmitter::emit_stack_load(Register rd, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit_lw(rd, Register::sp, offset);
        return;
    }

    if (scratch == rd) {
        throw std::runtime_error("stack load scratch register conflicts with destination register");
    }

    // if imm out of range, use scrath register to store stack frame address
    emit_stack_address(scratch, offset);
    emit_lw(rd, scratch, 0);
}

// store rs to stack
void IREmitter::emit_stack_store(Register rs, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        emit_sw(rs, Register::sp, offset);
        return;
    }

    if (scratch == rs) {
        throw std::runtime_error("stack store scratch register conflicts with source register");
    }

    // if imm out of range, use scrath register to store stack frame address
    emit_stack_address(scratch, offset);
    emit_sw(rs, scratch, 0);
}

// ===== Raw assembly emission =====

void IREmitter::emit_li(Register rd, int32_t imm)
{
    out_ << "  li " << reg_name(rd) << ", " << imm << "\n";
}

void IREmitter::emit_mv(Register rd, Register rs)
{
    out_ << "  mv " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void IREmitter::emit_add(Register rd, Register rs1, Register rs2)
{
    out_ << "  add " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_addi(Register rd, Register rs1, int32_t imm)
{
    out_ << "  addi " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << imm << "\n";
}

void IREmitter::emit_sub(Register rd, Register rs1, Register rs2)
{
    out_ << "  sub " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_mul(Register rd, Register rs1, Register rs2)
{
    out_ << "  mul " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_div(Register rd, Register rs1, Register rs2)
{
    out_ << "  div " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_rem(Register rd, Register rs1, Register rs2)
{
    out_ << "  rem " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_and(Register rd, Register rs1, Register rs2)
{
    out_ << "  and " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_or(Register rd, Register rs1, Register rs2)
{
    out_ << "  or " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_xor(Register rd, Register rs1, Register rs2)
{
    out_ << "  xor " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_slt(Register rd, Register rs1, Register rs2)
{
    out_ << "  slt " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_sll(Register rd, Register rs1, Register rs2)
{
    out_ << "  sll " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_srl(Register rd, Register rs1, Register rs2)
{
    out_ << "  srl " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_sra(Register rd, Register rs1, Register rs2)
{
    out_ << "  sra " << reg_name(rd) << ", " << reg_name(rs1)
         << ", " << reg_name(rs2) << "\n";
}

void IREmitter::emit_seqz(Register rd, Register rs)
{
    out_ << "  seqz " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void IREmitter::emit_snez(Register rd, Register rs)
{
    out_ << "  snez " << reg_name(rd) << ", " << reg_name(rs) << "\n";
}

void IREmitter::emit_la(Register rd, const std::string& label)
{
    out_ << "  la " << reg_name(rd) << ", " << label << "\n";
}

void IREmitter::emit_lw(Register rd, Register rs1, int32_t offset)
{
    out_ << "  lw " << reg_name(rd) << ", " << offset
         << "(" << reg_name(rs1) << ")\n";
}

void IREmitter::emit_sw(Register rs2, Register rs1, int32_t offset)
{
    out_ << "  sw " << reg_name(rs2) << ", " << offset
         << "(" << reg_name(rs1) << ")\n";
}

void IREmitter::emit_ret()
{
    out_ << "  ret\n";
}

void IREmitter::emit_call_label(const std::string& label)
{
    out_ << "  call " << label << "\n";
}

void IREmitter::emit_bnez(Register rs, const std::string& label)
{
    out_ << "  bnez " << reg_name(rs) << ", " << label << "\n";
}

void IREmitter::emit_j(const std::string& label)
{
    out_ << "  j " << label << "\n";
}

void emit_module(const rewind_ir::IRModule& module, std::ostream& out)
{
    IREmitter emitter(out);
    emitter.emit_module(module);
}

} // namespace riscv
