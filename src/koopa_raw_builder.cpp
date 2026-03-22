#include "koopa_ir.h"

#include <stdexcept>
#include <vector>

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

    std::string func_name = function.name;
    if (func_name.empty() || func_name[0] != '@') {
        func_name = "@" + func_name;
    }
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

    std::string block_name = block.name;
    if (!block_name.empty() && block_name[0] != '%') {
        block_name = "%" + block_name;
    }
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
        const auto value = new_integer(inst.ret_value);
        return new_return(value);
    }
    }
    throw std::runtime_error("Unsupported instruction kind in raw lowering");
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

    std::runtime_error make_dump_error(const char* msg, koopa_error_code_t ec)
    {
        return std::runtime_error(std::string(msg) + ", error code=" + std::to_string(static_cast<int>(ec)));
    }

    std::string dump_with_api(koopa_program_t program,
        koopa_error_code_t (*dump_fn)(koopa_program_t, char*, size_t*))
    {
        size_t len = 0;
        auto ec = dump_fn(program, nullptr, &len);
        if (ec != KOOPA_EC_SUCCESS) {
            throw make_dump_error("libkoopa dump length query failed", ec);
        }

        std::vector<char> buffer(len + 1, '\0');
        size_t cap = buffer.size();
        ec = dump_fn(program, buffer.data(), &cap);
        if (ec != KOOPA_EC_SUCCESS) {
            throw make_dump_error("libkoopa dump failed", ec);
        }
        return std::string(buffer.data());
    }

} // namespace

std::string dump_koopa_program_to_string(koopa_program_t program)
{
    return dump_with_api(program, koopa_dump_to_string);
}

} // namespace koopa_ir
