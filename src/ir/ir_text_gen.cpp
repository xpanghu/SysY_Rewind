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
        return IRErrorCode::INVALID_ARGUMENT;
    }

    try {
        for (const auto* value : module.global_values_) {
            print_global_value(value, out);
        }

        out << "\n";

        const IRFunction* previous = nullptr;
        for (const auto* func : module.funcs_) {
            print_function(previous, func, out);
            previous = func;
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

void IRTextGen::print_global_value(const IRValue* value, std::ostream& out)
{
    const auto* global_alloc = value->as<IRGlobalAllocInst>();
    const auto* alloc_type = global_alloc->type_->as<IRPointerType>();

    out << "global " << global_alloc->name_ << " = alloc ";
    print_type(alloc_type->base_type, out);
    out << ", ";

    switch (global_alloc->init_->kind_) {
    case IRValueKind::IR_ZERO_INIT:
    case IRValueKind::IR_INTEGER:
    case IRValueKind::IR_AGGREGATE:
        print_value(global_alloc->init_, out);
        break;
    default:
        throw std::runtime_error("unsupported global initializer");
    }

    out << "\n";
}

void IRTextGen::print_function(const IRFunction* previous,
                               const IRFunction* current,
                               std::ostream& out)
{
    if (previous != nullptr
        && previous->is_declaration_
        && !current->is_declaration_) {
        out << "\n";
    }

    // function declaration
    if (current->is_declaration_) {
        out << "decl @" << current->name_ << "(";
        for (size_t i = 0; i < current->type_->params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            print_type(current->type_->params[i], out);
        }
        out << ")";
        if (!current->type_->return_type->is_unit()) {
            out << ": ";
            print_type(current->type_->return_type, out);
        }
        out << "\n";
        return;
    }

    // function definition
    out << "fun @" << current->name_ << "(";
    for (size_t i = 0; i < current->params_.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        print_value(current->params_[i], out);
        out << ": ";
        print_type(current->type_->params[i], out);
    }
    out << ")";

    if (!current->type_->return_type->is_unit()) {
        out << ": ";
        print_type(current->type_->return_type, out);
    }

    out << " {\n";
    // print all blocks
    for (const auto* block : current->basic_blocks_) {
        print_basic_block(block, out);
        if (block != current->basic_blocks_.back()) {
            out << "\n";
        }
    }
    out << "}\n\n";
}

void IRTextGen::print_basic_block(const IRBasicBlock* block, std::ostream& out)
{
    // basic block name: %entry: or %merge(%x: i32):
    out << block->name_;
    if (!block->params_.empty()) {
        out << "(";
        for (size_t i = 0; i < block->params_.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            print_value(block->params_[i], out);
            out << ": ";
            print_type(block->params_[i]->type_, out);
        }
        out << ")";
    }
    out << ":\n";

    // print all insts
    for (const auto* inst : block->insts_) {
        std::ostringstream line;
        print_instruction(inst, line);
        if (!line.str().empty()) {
            out << line.str() << "\n";
        }
    }
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
        out << "alloc ";
        if (alloc->type_->is_pointer()) {
            const auto* alloc_type = alloc->type_->as<IRPointerType>();
            print_type(alloc_type->base_type, out);
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
    case rewind_ir::IRValueKind::IR_GET_PTR: {
        const auto* get_ptr = inst->as<IRGetPtrInst>();
        out << "  " << get_ptr->name_ << " = getptr ";
        print_value(get_ptr->src_, out);
        out << ", ";
        print_value(get_ptr->index_, out);
        break;
    }
    case rewind_ir::IRValueKind::IR_GET_ELEM_PTR: {
        const auto* get_elem_ptr = inst->as<IRGetElemPtrInst>();
        out << "  " << get_elem_ptr->name_ << " = getelemptr ";
        print_value(get_elem_ptr->src_, out);
        out << ", ";
        print_value(get_elem_ptr->index_, out);
        break;
    }
    case rewind_ir::IRValueKind::IR_CALL: {
        const auto* call = inst->as<IRCallInst>();
        out << "  ";
        if (!call->type_->is_unit()) {
            out << call->name_ << " = ";
        }
        out << "call @" << call->callee_->name_ << "(";
        for (size_t i = 0; i < call->args_.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            print_value(call->args_[i], out);
        }
        out << ")";
        break;
    }
    case rewind_ir::IRValueKind::IR_BRANCH: {
        const auto* branch = inst->as<IRBranchInst>();
        const auto* if_bb = branch->if_basic_block_;
        const auto* else_bb = branch->else_basic_block_;
        const auto print_successor = [this, &out](const IRBasicBlock* block,
                                                   const std::vector<IRValue*>& args) {
            out << block->name_;
            if (args.empty()) {
                return;
            }
            out << "(";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                print_value(args[i], out);
            }
            out << ")";
        };

        out << "  " << "br ";
        print_value(branch->cond_, out);
        out << ", ";
        print_successor(if_bb, branch->if_args_);
        out << ", ";
        print_successor(else_bb, branch->else_args_);
        break;
    }
    case rewind_ir::IRValueKind::IR_JUMP: {
        const auto* jump = inst->as<IRJumpInst>();
        const auto* jump_bb = jump->jump_basic_block_;

        out << "  " << "jump ";
        out << jump_bb->name_;
        if (!jump->args_.empty()) {
            out << "(";
            for (size_t i = 0; i < jump->args_.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                print_value(jump->args_[i], out);
            }
            out << ")";
        }
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
void IRTextGen::print_value(const IRValue* value, std::ostream& out)
{
    if (value->is_integer()) {
        const auto* c = value->as<IRConstant>();
        out << c->value_;
        return;
    }

    if (value->is_zero_init()) {
        out << "zeroinit";
        return;
    }

    if (value->is_aggregate()) {
        const auto* aggregate = value->as<IRAggregate>();
        out << "{";
        for (size_t i = 0; i < aggregate->elems_.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            print_value(aggregate->elems_[i], out);
        }
        out << "}";
        return;
    }

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
        out << "[";
        print_type(array_type->element_type, out);
        out << ", " << array_type->length;
        out << "]";
        break;
    }
    case IRTypeTag::POINTER: {
        auto* pointer_type = type->as<IRPointerType>();
        out << "*";
        print_type(pointer_type->base_type, out);
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
