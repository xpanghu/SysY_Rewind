#include "koopa_ir.h"

namespace koopa_ir {

KoopaRawBuilder::KoopaRawBuilder()
{
    int32_type_.tag = KOOPA_RTT_INT32;
    unit_type_.tag = KOOPA_RTT_UNIT;
    reset();
}

koopa_raw_program_t KoopaRawBuilder::build(const IRProgram& program)
{
    reset();

    for (const auto& function : program.functions) {
        functions_.push_back(lower_function(function));
    }

    program_.values = make_slice(global_values_, KOOPA_RSIK_VALUE);
    program_.funcs = make_slice(functions_, KOOPA_RSIK_FUNCTION);
    return program_;
}

void KoopaRawBuilder::reset()
{
    program_ = {};
    names_.clear();
    function_type_kinds_.clear();
    value_datas_.clear();
    bb_datas_.clear();
    func_datas_.clear();
    global_values_.clear();
    functions_.clear();
    current_blocks_.clear();
    current_insts_.clear();
    current_virtual_values_.clear();
}

koopa_raw_type_t KoopaRawBuilder::lower_type(const std::string& type_name)
{
    if (type_name == "i32") {
        return &int32_type_;
    }
    if (type_name == "unit") {
        return &unit_type_;
    }
    throw std::runtime_error("Unsupported IR type: " + type_name);
}

koopa_raw_function_t KoopaRawBuilder::lower_function(const IRFunction& function)
{
    current_blocks_.clear();
    current_virtual_values_.clear();

    for (const auto& block : function.blocks) {
        current_blocks_.push_back(lower_basic_block(block));
    }

    koopa_raw_type_kind_t function_type {};
    function_type.tag = KOOPA_RTT_FUNCTION;
    function_type.data.function.params = make_empty_slice(KOOPA_RSIK_TYPE);
    function_type.data.function.ret = lower_type(function.ret_type);
    function_type_kinds_.push_back(function_type);

    koopa_raw_function_data_t func_data {};
    func_data.ty = &function_type_kinds_.back();

    std::string func_name = to_raw_specific_name(function.name);
    func_data.name = save_name(func_name);
    func_data.params = make_empty_slice(KOOPA_RSIK_VALUE);
    func_data.bbs = make_slice(current_blocks_, KOOPA_RSIK_BASIC_BLOCK);

    func_datas_.push_back(func_data);
    return &func_datas_.back();
}

koopa_raw_basic_block_t KoopaRawBuilder::lower_basic_block(const IRBasicBlock& block)
{
    current_insts_.clear();

    for (const auto& inst : block.insts) {
        current_insts_.push_back(lower_instruction(inst));
    }

    koopa_raw_basic_block_data_t bb_data {};

    std::string block_name = to_raw_temp_name(block.name);
    bb_data.name = block_name.empty() ? nullptr : save_name(block_name);
    bb_data.params = make_empty_slice(KOOPA_RSIK_VALUE);
    bb_data.used_by = make_empty_slice(KOOPA_RSIK_VALUE);
    bb_data.insts = make_slice(current_insts_, KOOPA_RSIK_VALUE);

    bb_datas_.push_back(bb_data);
    return &bb_datas_.back();
}

koopa_raw_value_t KoopaRawBuilder::lower_instruction(const IRInstruction& inst)
{
    switch (inst.kind) {
    case IRInstruction::Kind::kRet: {
        const auto value = lower_ir_value(inst.dst);
        return new_return(value);
    }
    case IRInstruction::Kind::kUnary: {
        const auto lhs = lower_ir_value(inst.lhs);
        const auto rhs = lower_ir_value(inst.rhs);
        const auto op = lower_binary_op(inst.op);

        std::string value_name;
        if (inst.dst.kind == IRValue::Kind::VIRTUAL_REGISTER) {
            value_name = "%" + std::to_string(inst.dst.virtual_register_name);
        }

        const auto value = new_binary(op, lhs, rhs, value_name);
        bind_virtual_register(inst.dst, value);
        return value;
    }
    }
    throw std::runtime_error("Unsupported instruction kind in raw lowering");
}

koopa_raw_value_t KoopaRawBuilder::lower_ir_value(const IRValue& value)
{
    if (value.kind == IRValue::Kind::IMMEDIATE) {
        return new_integer(value.imm);
    }

    if (value.kind == IRValue::Kind::VIRTUAL_REGISTER) {
        const int id = value.virtual_register_name;
        if (id < 0 || static_cast<size_t>(id) >= current_virtual_values_.size() || current_virtual_values_[id] == nullptr) {
            throw std::runtime_error("use of undefined virtual register: %" + std::to_string(id));
        }
        return current_virtual_values_[id];
    }

    throw std::runtime_error("unsupported IRValue kind");
}

koopa_raw_binary_op_t KoopaRawBuilder::lower_binary_op(IRInstruction::Op op)
{
    switch (op) {
    case IRInstruction::Op::ADD:
        return KOOPA_RBO_ADD;
    case IRInstruction::Op::SUB:
        return KOOPA_RBO_SUB;
    case IRInstruction::Op::EQ:
        return KOOPA_RBO_EQ;
    }
    throw std::runtime_error("unsupported IR binary op");
}

void KoopaRawBuilder::bind_virtual_register(const IRValue& value, koopa_raw_value_t raw_value)
{
    if (value.kind != IRValue::Kind::VIRTUAL_REGISTER) {
        return;
    }

    const int id = value.virtual_register_name;
    if (id < 0) {
        throw std::runtime_error("invalid virtual register id");
    }

    if (static_cast<size_t>(id) >= current_virtual_values_.size()) {
        current_virtual_values_.resize(static_cast<size_t>(id) + 1, nullptr);
    }
    current_virtual_values_[id] = raw_value;
}

koopa_raw_value_t KoopaRawBuilder::new_integer(int value)
{
    koopa_raw_value_data_t data {};
    data.ty = &int32_type_;
    data.name = nullptr;
    data.used_by = make_empty_slice(KOOPA_RSIK_VALUE);
    data.kind.tag = KOOPA_RVT_INTEGER;
    data.kind.data.integer.value = value;
    value_datas_.push_back(data);
    return &value_datas_.back();
}

koopa_raw_value_t KoopaRawBuilder::new_binary(koopa_raw_binary_op_t op,
    koopa_raw_value_t lhs, koopa_raw_value_t rhs,
    const std::string& name)
{
    koopa_raw_value_data_t data {};
    data.ty = &int32_type_;
    data.name = name.empty() ? nullptr : save_name(name);
    data.used_by = make_empty_slice(KOOPA_RSIK_VALUE);
    data.kind.tag = KOOPA_RVT_BINARY;
    data.kind.data.binary.op = op;
    data.kind.data.binary.lhs = lhs;
    data.kind.data.binary.rhs = rhs;
    value_datas_.push_back(data);
    return &value_datas_.back();
}

koopa_raw_value_t KoopaRawBuilder::new_return(koopa_raw_value_t value)
{
    koopa_raw_value_data_t data {};
    data.ty = &unit_type_;
    data.name = nullptr;
    data.used_by = make_empty_slice(KOOPA_RSIK_VALUE);
    data.kind.tag = KOOPA_RVT_RETURN;
    data.kind.data.ret.value = value;
    value_datas_.push_back(data);
    return &value_datas_.back();
}

std::string KoopaRawBuilder::to_raw_specific_name(const std::string& name)
{
    if (name.empty() || name[0] != '@') {
        return "@" + name;
    }
    return name;
}

std::string KoopaRawBuilder::to_raw_temp_name(const std::string& name)
{
    if (!name.empty() && name[0] != '%') {
        return "%" + name;
    }
    return name;
}

const char* KoopaRawBuilder::save_name(const std::string& name)
{
    names_.push_back(name);
    return names_.back().c_str();
}

koopa_raw_slice_t KoopaRawBuilder::make_slice(std::vector<const void*>& items,
    koopa_raw_slice_item_kind_t kind)
{
    koopa_raw_slice_t slice {};
    slice.buffer = items.empty() ? nullptr : const_cast<const void**>(items.data());
    slice.len = static_cast<uint32_t>(items.size());
    slice.kind = kind;
    return slice;
}

koopa_raw_slice_t KoopaRawBuilder::make_empty_slice(koopa_raw_slice_item_kind_t kind)
{
    koopa_raw_slice_t slice {};
    slice.buffer = nullptr;
    slice.len = 0;
    slice.kind = kind;
    return slice;
}

namespace {
    std::string dump_with_api(koopa_program_t program,
        koopa_error_code_t (*dump_fn)(koopa_program_t, char*, size_t*))
    {
        size_t len = 0;
        auto ec = dump_fn(program, nullptr, &len);
        assert(ec == KOOPA_EC_SUCCESS);

        std::vector<char> buffer(len + 1, '\0');
        size_t cap = buffer.size();
        ec = dump_fn(program, buffer.data(), &cap);

        assert(ec == KOOPA_EC_SUCCESS);
        return std::string(buffer.data());
    }

} // namespace

std::string dump_koopa_program_to_string(koopa_program_t program)
{
    return dump_with_api(program, koopa_dump_to_string);
}

} // namespace koopa_ir
