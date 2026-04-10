#include "ir_text_gen.h"
#include "rewind_ir.h"

namespace rewind_ir
{
/*
 * generate IR text
 */

IRErrorCode IRTextGen::emit(const IRModule& module, std::ostream& out)
{
    last_error_.clear();
    if (module.funcs_.empty()) {
        last_error_ = "module has no functions";
        return IRErrorCode::INVALID_ARGUMENT;
    }

    try {
        for (const auto* value : module.global_values_) {
            print_global_value(value, out);
        }

        for (const auto* func : module.funcs_) {
            print_function(func, out);
        }

        return IRErrorCode::SUCCESS;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        out << "; IRTextGen::emit error: " << last_error_ << "\n";
        out.flush();
        return IRErrorCode::GENERATION_ERROR;
    }
}

IRErrorCode IRTextGen::emit_to_string(const IRModule& module, std::string& out)
{
    std::ostringstream oss;
    auto ret = emit(module, oss);
    if (ret == IRErrorCode::SUCCESS) {
        out = oss.str();
    }
    return ret;
}

// todo
void IRTextGen::print_global_value(const IRValue* value, std::ostream& out)
{
}

void IRTextGen::print_function(const IRFunction* func, std::ostream& out)
{
    // function head：fun @main(): i32 {
    out << "fun @" << func->name_ << "(): ";
    print_type(func->type_, out);
    out << " {\n";

    // print all blocks
    for (const auto* block : func->basic_blocks_) {
        print_basic_block(block, out);
    }

    out << "}\n";
}

void IRTextGen::print_basic_block(const IRBasicBlock* block, std::ostream& out)
{
    // basic block name：%entry:
    out << block->name_ << ":\n";

    // print all insts
    for (const auto* inst : block->insts_) {
        std::ostringstream line;
        print_instruction(inst, line);
        if (!line.str().empty()) {
            out << line.str() << "\n";
        }
    }

    out << "\n";
}

void IRTextGen::print_instruction(const IRValue* inst, std::ostream& out)
{
    switch (inst->kind_) {
    case rewind_ir::IRValueKind::IR_INTEGER: {
        break;
    }
    case rewind_ir::IRValueKind::IR_BINARY: {
        const auto* binary = inst->as<IRBinaryInst>();
        out << "  " << binary->name_ << " = ";
        print_binary_op(binary->op_, out);
        out << " ";
        print_value(binary->lhs_, out);
        out << ", ";
        print_value(binary->rhs_, out);
        break;
    }
    case rewind_ir::IRValueKind::IR_RETURN: {
        const auto* ret = inst->as<IRReturnInst>();
        out << "  ret";
        if (ret->dst_ != nullptr) {
            out << " ";
            print_value(ret->dst_, out);
        }
        break;
    }
    case rewind_ir::IRValueKind::IR_ALLOC: {
        const auto* alloc = inst->as<IRAllocInst>();
        out << "  " << alloc->name_ << " = ";
        if (alloc->type_->is_pointer()) {
            const auto* alloc_type = alloc->type_->as<IRPointerType>();
            out << "alloc ";
            print_type(alloc_type->base_type, out);
        } else {
            throw std::runtime_error("alloc instruction must have pointer type");
        }
        break;
    }
    case rewind_ir::IRValueKind::IR_LOAD: {
        const auto* load = inst->as<IRLoadInst>();
        out << "  " << load->name_ << " = ";
        out << "load " << load->src_->name_;
        break;
    }
    case rewind_ir::IRValueKind::IR_STORE: {
        const auto* store = inst->as<IRStoreInst>();
        out << "  " << "store ";
        print_value(store->value_, out);
        out << ", ";
        print_value(store->dest_, out);
        break;
    }
    case rewind_ir::IRValueKind::IR_BRANCH: {
        const auto* branch = inst->as<IRBranchInst>();
        const auto* if_bb = branch->if_basic_block_;
        const auto* else_bb = branch->else_basic_block_;

        out << "  " << "br ";
        print_value(branch->cond_, out);
        out << ", ";
        out << if_bb->name_ << ", ";
        out << else_bb->name_;
        break;
    }
    case rewind_ir::IRValueKind::IR_JUMP: {
        const auto* jump = inst->as<IRJumpInst>();
        const auto* jump_bb = jump->jump_basic_block_;

        out << "  " << "jump ";
        out << jump_bb->name_;
        break;
    }
    default: {
        throw std::runtime_error(
            "Unsupported instruction type: name=" + inst->name_
            + ", kind=" + std::to_string(static_cast<int>(inst->kind_)));
    }
    }
}

// print operand
// const or inst result
void IRTextGen::print_value(const IRValue* value, std::ostream& out)
{
    // const
    if (value->is_integer()) {
        const auto* c = value->as<IRConstant>();
        out << c->value_;
        return;
    }

    // virtual register
    out << value->name_;
}

void IRTextGen::print_type(const IRType* type, std::ostream& out)
{
    if (type == nullptr) {
        out << "void";
        return;
    }

    switch (type->tag) {
    case IRTypeTag::INT32: {
        out << "i32";
        break;
    }
    case IRTypeTag::UNIT: {
        out << "unit";
        break;
    }
    case IRTypeTag::ARRAY: {
        auto* array_type = type->as<IRArrayType>();
        out << "array[" << array_type->length << " x ";
        print_type(array_type->element_type, out);
        out << "]";
        break;
    }
    case IRTypeTag::POINTER: {
        auto* pointer_type = type->as<IRPointerType>();
        out << "pointer<";
        print_type(pointer_type->base_type, out);
        out << ">";
        break;
    }
    case IRTypeTag::FUNCTION: {
        out << "function<";
        out << "(";
        auto* function_type = type->as<IRFunctionType>();
        for (size_t i = 0; i < function_type->params.size(); i++) {
            if (i > 0) out << ", ";
            print_type(function_type->params[i], out);
        }
        out << ") -> ";
        print_type(function_type->return_type, out);
        out << ">";
        break;
    }
    default:
        throw std::runtime_error("Unsupported IR type");
    }
}

void IRTextGen::print_binary_op(IRBinaryOp op, std::ostream& out)
{
    switch (op) {
    case IRBinaryOp::ADD:
        out << "add";
        break;
    case IRBinaryOp::SUB:
        out << "sub";
        break;
    case IRBinaryOp::MUL:
        out << "mul";
        break;
    case IRBinaryOp::DIV:
        out << "div";
        break;
    case IRBinaryOp::MOD:
        out << "mod";
        break;
    case IRBinaryOp::EQ:
        out << "eq";
        break;
    case IRBinaryOp::NEQ:
        out << "ne";
        break;
    case IRBinaryOp::LT:
        out << "lt";
        break;
    case IRBinaryOp::GT:
        out << "gt";
        break;
    case IRBinaryOp::LE:
        out << "le";
        break;
    case IRBinaryOp::GE:
        out << "ge";
        break;
    case IRBinaryOp::AND:
        out << "and";
        break;
    case IRBinaryOp::OR:
        out << "or";
        break;
    case IRBinaryOp::XOR:
        out << "xor";
        break;
    case IRBinaryOp::SHL:
        out << "shl";
        break;
    case IRBinaryOp::SHR:
        out << "shr";
        break;
    case IRBinaryOp::SAR:
        out << "sar";
        break;
    default:
        throw std::runtime_error("Unsupported binary op");
    }
}
} // namespace rewind_ir
