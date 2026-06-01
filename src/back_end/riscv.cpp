#include <cstdint>
#include "riscv.h"
#include "machine_asm_printer.h"
#include "ir_type.h"
#include "rewind_ir.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace riscv
{

namespace
{

inline Register pick_scratch(Register avoid1, Register avoid2 = Register::x0)
{
    for (Register reg : {Register::t0, Register::t1, Register::t2}) {
        if (reg != avoid1 && reg != avoid2) {
            return reg;
        }
    }
    throw std::runtime_error("no available scratch register");
}

// return array type
inline const rewind_ir::IRArrayType* get_array_storage_type(const rewind_ir::IRValue* value)
{
    if (!value->type_->is_pointer()) {
        return nullptr;
    }

    const auto* pointer_base_type = value->type_->as<rewind_ir::IRPointerType>()->base_type;
    if (!pointer_base_type->is_array()) {
        return nullptr;
    }

    return pointer_base_type->as<rewind_ir::IRArrayType>();
}

// const rewind_ir::IRType* get_array_storage_type(const IRValue* storage)

} // namespace

IREmitter::IREmitter(std::ostream& out) : writer_(out)
{
}

// imm12 scope [-2048, 2047]
// check value is in scope
bool IREmitter::fits_i12(int32_t value)
{
    return value >= -2048 && value <= 2047;
}

/*
 * from @name to name
 * exmple: @abc_1 -> abc_1
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
    const std::string& name = sanitize_symbol(global_alloc.name_);
    if (global_alloc.type_ == nullptr || !global_alloc.type_->is_pointer()) {
        throw std::runtime_error("global alloc must have pointer type");
    }
    const auto* alloc_type = global_alloc.type_->as<rewind_ir::IRPointerType>();

    writer_.section_data();
    writer_.global(name);
    writer_.label(name);

    emit_global_initializer(global_alloc.init_, alloc_type->base_type);

    writer_.blank_line();
}

void IREmitter::emit_global_initializer(const rewind_ir::IRValue* init,
                                        const rewind_ir::IRType* type)
{
    if (init->is_zero_init()) {
        const auto size = data_layout_.type_size(type);
        writer_.zero(size);
        return;
    }

    if (type->is_int32()) {
        const auto* constant = init->as<rewind_ir::IRConstant>();
        writer_.word(constant->value_);
        return;
    }

    if (type->is_array()) {
        const auto* array_type = type->as<rewind_ir::IRArrayType>();
        const auto* aggregate = init->as<rewind_ir::IRAggregate>();

        for (const auto* elem : aggregate->elems_) {
            emit_global_initializer(elem, array_type->element_type);
        }
        return;
    }

    throw std::runtime_error("unsupported global initializer type");
}

void IREmitter::emit_function(const rewind_ir::IRFunction& func)
{
    writer_.section_text();
    writer_.globl(sanitize_symbol(func.name_));

    // cal stack frame size
    current_function_ = &func;
    frame_.build(func);

    writer_.label(sanitize_symbol(func.name_));
    emit_prologue();

    for (const auto* block : func.basic_blocks_) {
        emit_basic_block(*block);
    }

    writer_.blank_line();
}

std::string IREmitter::basic_block_label(const rewind_ir::IRBasicBlock& block) const
{
    return ".L" + sanitize_symbol(current_function_->name_) + "_"
           + sanitize_symbol(block.name_);
}

void IREmitter::emit_basic_block(const rewind_ir::IRBasicBlock& block)
{
    writer_.label(basic_block_label(block));

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
    case rewind_ir::IRValueKind::IR_JUMP:
        emit_jump(*inst.as<rewind_ir::IRJumpInst>());
        return;
    case rewind_ir::IRValueKind::IR_BRANCH:
        emit_branch(*inst.as<rewind_ir::IRBranchInst>());
        return;
    case rewind_ir::IRValueKind::IR_GET_PTR:
        emit_get_ptr(*inst.as<rewind_ir::IRGetPtrInst>());
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
void IREmitter::emit_get_ptr(const rewind_ir::IRGetPtrInst& inst)
{
    if (inst.src_ == nullptr || inst.src_->type_ == nullptr || !inst.src_->type_->is_pointer()) {
        throw std::runtime_error("getptr source is not pointer value");
    }

    const auto* pointee_type = inst.src_->type_->as<rewind_ir::IRPointerType>()->base_type;
    materialize_value(inst.src_, Register::t0);
    materialize_value(inst.index_, Register::t1);

    const auto elem_size = data_layout_.type_size(pointee_type);
    if (elem_size != 1) {
        writer_.li(Register::t2, elem_size);
        writer_.mul(Register::t1, Register::t1, Register::t2);
    }

    writer_.add(Register::t0, Register::t0, Register::t1);
    spill_value(&inst, Register::t0);
}

void IREmitter::emit_get_elem_ptr(const rewind_ir::IRGetElemPtrInst& inst)
{
    const auto* array_type = get_array_storage_type(inst.src_);
    if (array_type == nullptr) {
        throw std::runtime_error("getelemptr source is not array storage");
    }

    materialize_pointer(inst.src_, Register::t0);
    materialize_value(inst.index_, Register::t1);

    const auto elem_size = data_layout_.type_size(array_type->element_type);
    if (elem_size != 1) {
        writer_.li(Register::t2, elem_size);
        writer_.mul(Register::t1, Register::t1, Register::t2);
    }

    writer_.add(Register::t0, Register::t0, Register::t1);
    spill_value(&inst, Register::t0);
}

void IREmitter::emit_call(const rewind_ir::IRCallInst& inst)
{
    const auto register_arg_count =
        std::min(inst.args_.size(), CallingConvention::ArgRegisterCount);

    // assign registers for args
    for (size_t i = 0; i < register_arg_count; ++i) {
        materialize_value(inst.args_[i], calling_convention_.arg_register(i));
    }

    // assign additional arg to the stack frame
    for (size_t i = CallingConvention::ArgRegisterCount; i < inst.args_.size(); ++i) {
        materialize_value(inst.args_[i], Register::t0);
        emit_stack_store(Register::t0, frame_.outgoing_arg_offset(i));
    }

    writer_.call_label(sanitize_symbol(inst.callee_->name_));

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
    writer_.bnez(Register::t0, if_label);
    const auto else_label = basic_block_label(*inst.else_basic_block_);
    writer_.j(else_label);
}

// jump to other inst
void IREmitter::emit_jump(const rewind_ir::IRJumpInst& inst)
{
    const auto label = basic_block_label(*inst.jump_basic_block_);
    writer_.j(label);
}

// local alloc will do nothing
void IREmitter::emit_alloc(const rewind_ir::IRAllocInst& inst)
{
    return;
}

// emit store inst
void IREmitter::emit_store(const rewind_ir::IRStoreInst& inst)
{
    // store local array
    if (inst.value_->is_aggregate() || inst.value_->is_zero_init()) {
        const auto* dest_type = inst.dest_->type_->as<rewind_ir::IRPointerType>()->base_type;

        materialize_pointer(inst.dest_, Register::t2);
        emit_initializer_store(inst.value_, dest_type, Register::t2, 0);
        return;
    }

    // store local scalar
    materialize_value(inst.value_, Register::t0);
    store_to_addressable(inst.dest_, Register::t0);
}

void IREmitter::emit_initializer_store(const rewind_ir::IRValue* init,
                                       const rewind_ir::IRType* type,
                                       Register base,
                                       int32_t offset)
{
    if (type->is_int32()) {
        if (init->is_zero_init()) {
            if (fits_i12(offset)) {
                writer_.sw(Register::x0, base, offset);
            } else {
                writer_.li(Register::t1, offset);
                writer_.add(Register::t1, base, Register::t1);
                writer_.sw(Register::x0, Register::t1, 0);
            }
            return;
        }

        materialize_value(init, Register::t0);
        if (fits_i12(offset)) {
            writer_.sw(Register::t0, base, offset);
        } else {
            writer_.li(Register::t1, offset);
            writer_.add(Register::t1, base, Register::t1);
            writer_.sw(Register::t0, Register::t1, 0);
        }
        return;
    }

    // array type
    const auto* array_type = type->as<rewind_ir::IRArrayType>();
    // get array element size
    const auto elem_size = data_layout_.type_size(array_type->element_type);

    if (init->is_zero_init()) {
        for (size_t i = 0; i < array_type->length; ++i) {
            emit_initializer_store(
                init,
                array_type->element_type,
                base,
                offset + static_cast<int32_t>(i) * elem_size);
        }
        return;
    }

    const auto* aggregate = init->as<rewind_ir::IRAggregate>();

    for (size_t i = 0; i < aggregate->elems_.size(); ++i) {
        emit_initializer_store(
            aggregate->elems_[i],
            array_type->element_type,
            base,
            offset + static_cast<int32_t>(i) * elem_size);
    }
}

void IREmitter::emit_load(const rewind_ir::IRLoadInst& inst)
{
    materialize_pointer(inst.src_, Register::t0);
    writer_.lw(Register::t0, Register::t0, 0);

    // store inst result to stack frame(inst)
    spill_value(&inst, Register::t0);
}

void IREmitter::emit_binary(const rewind_ir::IRBinaryInst& inst)
{
    materialize_value(inst.lhs_, Register::t0);
    materialize_value(inst.rhs_, Register::t1);

    switch (inst.op_) {
    case rewind_ir::IRBinaryOp::ADD:
        writer_.add(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SUB:
        writer_.sub(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::MUL:
        writer_.mul(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::DIV:
        writer_.div(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::MOD:
        writer_.rem(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::AND:
        writer_.and_(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::OR:
        writer_.or_(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::XOR:
        writer_.xor_(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::EQ:
        writer_.xor_(Register::t0, Register::t0, Register::t1);
        writer_.seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::NEQ:
        writer_.xor_(Register::t0, Register::t0, Register::t1);
        writer_.snez(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::LT:
        writer_.slt(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::GT:
        writer_.slt(Register::t0, Register::t1, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::LE:
        writer_.slt(Register::t0, Register::t1, Register::t0);
        writer_.seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::GE:
        writer_.slt(Register::t0, Register::t0, Register::t1);
        writer_.seqz(Register::t0, Register::t0);
        break;
    case rewind_ir::IRBinaryOp::SHL:
        writer_.sll(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SHR:
        writer_.srl(Register::t0, Register::t0, Register::t1);
        break;
    case rewind_ir::IRBinaryOp::SAR:
        writer_.sra(Register::t0, Register::t0, Register::t1);
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
    writer_.ret();
}

/*
 * Operand materialization helpers
 */

// load value to dst register
void IREmitter::materialize_value(const rewind_ir::IRValue* value, Register dst)
{
    // maybe throw runtime error
    if (value == nullptr) {
        throw std::runtime_error("materialize_value received nullptr");
    }

    // immediate
    if (value->is_integer()) {
        writer_.li(dst, value->as<rewind_ir::IRConstant>()->value_);
        return;
    }

    // func arg
    if (value->is_func_arg_ref()) {
        auto* func_arg_ref = value->as<rewind_ir::IRFuncArgRef>();
        const size_t param_index = static_cast<size_t>(func_arg_ref->index_);
        if (calling_convention_.is_register_arg(param_index)) {
            writer_.mv(dst, calling_convention_.arg_register(param_index));
        } else {
            emit_stack_load(dst, frame_.incoming_stack_arg_offset(param_index));
        }
        return;
    }

    // variable (global variable)
    if (value->is_global_alloc()) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        writer_.la(dst, sanitize_symbol(global_alloc->name_));
        writer_.lw(dst, dst, 0);
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

// load address(value) to dst register
void IREmitter::materialize_pointer(const rewind_ir::IRValue* value, Register dst)
{
    if (value == nullptr) {
        throw std::runtime_error("materialize_pointer received nullptr");
    }

    if (value->kind_ == rewind_ir::IRValueKind::IR_GLOBALALLOC) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        writer_.la(dst, sanitize_symbol(global_alloc->name_));
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

// store register src to stack(value address)
void IREmitter::spill_value(const rewind_ir::IRValue* value, Register src)
{
    if (!frame_.has_value_slot(value)) {
        throw std::runtime_error("missing spill slot for IR value: " + value->name_);
    }
    emit_stack_store(src, frame_.value_slot(value));
}

// Stack frame helpers

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
        writer_.addi(Register::sp, Register::sp, delta);
        return;
    }

    // load delta to t0 then add sp t0 to sp
    writer_.li(Register::t0, delta);
    writer_.add(Register::sp, Register::sp, Register::t0);
}

/*
 * Stack access helpers
 */

// store dst register to the address of value
void IREmitter::store_to_addressable(const rewind_ir::IRValue* value, Register src)
{
    // local variable
    if (frame_.has_object_slot(value)) {
        emit_stack_store(src, frame_.object_slot(value));
        return;
    }

    // global variable
    if (value->is_global_alloc()) {
        const auto* global_alloc = value->as<rewind_ir::IRGlobalAllocInst>();
        writer_.la(Register::t1, sanitize_symbol(global_alloc->name_));
        writer_.sw(src, Register::t1, 0);
        return;
    }

    if (frame_.has_value_slot(value)) {
        materialize_pointer(value, Register::t1);
        writer_.sw(src, Register::t1, 0);
        return;
    }

    throw std::runtime_error(value->name_ + " is not addressable");
}

// ?
// compute stack address and store in rd
void IREmitter::emit_stack_address(Register rd, int32_t offset)
{
    if (fits_i12(offset)) {
        writer_.addi(rd, Register::sp, offset);
        return;
    }

    writer_.li(rd, offset);
    writer_.add(rd, Register::sp, rd);
}

// load the value of stack address to rd
void IREmitter::emit_stack_load(Register rd, int32_t offset)
{
    if (fits_i12(offset)) {
        writer_.lw(rd, Register::sp, offset);
        return;
    }

    emit_stack_address(rd, offset);
    writer_.lw(rd, rd, 0);
}

// store rs to stack
void IREmitter::emit_stack_store(Register rs, int32_t offset, Register scratch)
{
    if (fits_i12(offset)) {
        writer_.sw(rs, Register::sp, offset);
        return;
    }

    Register addr_reg = scratch;
    if (addr_reg == rs) {
        addr_reg = pick_scratch(rs);
    }

    // if imm out of range, use scratch register to store stack frame address
    emit_stack_address(addr_reg, offset);
    writer_.sw(rs, addr_reg, 0);
}

void emit_module(const rewind_ir::IRModule& module, std::ostream& out)
{
    MachineAsmPrinter printer(out);
    printer.emit_module(module);
}

} // namespace riscv
