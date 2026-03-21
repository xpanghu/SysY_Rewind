#include "koopa_raw_builder.h"

#include <stdexcept>

namespace koopa_ir {

KoopaRawBuilder::KoopaRawBuilder()
{
    int32_type_.tag = KOOPA_RTT_INT32;
    unit_type_.tag = KOOPA_RTT_UNIT;
    Reset();
}

koopa_raw_program_t KoopaRawBuilder::Build(const IRProgram& program)
{
    Reset();

    for (const auto& function : program.functions) {
        functions_.push_back(LowerFunction(function));
    }

    program_.values = MakeSlice(global_values_, KOOPA_RSIK_VALUE);
    program_.funcs = MakeSlice(functions_, KOOPA_RSIK_FUNCTION);
    return program_;
}

void KoopaRawBuilder::Reset()
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

koopa_raw_type_t KoopaRawBuilder::LowerType(const std::string& type_name)
{
    if (type_name == "i32") {
        return &int32_type_;
    }
    if (type_name == "unit") {
        return &unit_type_;
    }
    throw std::runtime_error("Unsupported IR type: " + type_name);
}

koopa_raw_function_t KoopaRawBuilder::LowerFunction(const IRFunction& function)
{
    current_blocks_.clear();

    for (const auto& block : function.blocks) {
        current_blocks_.push_back(LowerBasicBlock(block));
    }

    koopa_raw_type_kind_t function_type {};
    function_type.tag = KOOPA_RTT_FUNCTION;
    function_type.data.function.params = MakeEmptySlice(KOOPA_RSIK_TYPE);
    function_type.data.function.ret = LowerType(function.ret_type);
    function_type_kinds_.push_back(function_type);

    koopa_raw_function_data_t func_data {};
    func_data.ty = &function_type_kinds_.back();

    std::string func_name = function.name;
    if (func_name.empty() || func_name[0] != '@') {
        func_name = "@" + func_name;
    }
    func_data.name = SaveName(func_name);
    func_data.params = MakeEmptySlice(KOOPA_RSIK_VALUE);
    func_data.bbs = MakeSlice(current_blocks_, KOOPA_RSIK_BASIC_BLOCK);

    func_datas_.push_back(func_data);
    return &func_datas_.back();
}

koopa_raw_basic_block_t KoopaRawBuilder::LowerBasicBlock(const IRBasicBlock& block)
{
    current_insts_.clear();

    for (const auto& inst : block.insts) {
        current_insts_.push_back(LowerInstruction(inst));
    }

    koopa_raw_basic_block_data_t bb_data {};

    std::string block_name = block.name;
    if (!block_name.empty() && block_name[0] != '%') {
        block_name = "%" + block_name;
    }
    bb_data.name = block_name.empty() ? nullptr : SaveName(block_name);
    bb_data.params = MakeEmptySlice(KOOPA_RSIK_VALUE);
    bb_data.used_by = MakeEmptySlice(KOOPA_RSIK_VALUE);
    bb_data.insts = MakeSlice(current_insts_, KOOPA_RSIK_VALUE);

    bb_datas_.push_back(bb_data);
    return &bb_datas_.back();
}

koopa_raw_value_t KoopaRawBuilder::LowerInstruction(const IRInstruction& inst)
{
    switch (inst.kind) {
    case IRInstruction::Kind::kRet: {
        const auto value = NewInteger(inst.ret_value);
        return NewReturn(value);
    }
    }
    throw std::runtime_error("Unsupported instruction kind in raw lowering");
}

koopa_raw_value_t KoopaRawBuilder::NewInteger(int value)
{
    koopa_raw_value_data_t data {};
    data.ty = &int32_type_;
    data.name = nullptr;
    data.used_by = MakeEmptySlice(KOOPA_RSIK_VALUE);
    data.kind.tag = KOOPA_RVT_INTEGER;
    data.kind.data.integer.value = value;
    value_datas_.push_back(data);
    return &value_datas_.back();
}

koopa_raw_value_t KoopaRawBuilder::NewReturn(koopa_raw_value_t value)
{
    koopa_raw_value_data_t data {};
    data.ty = &unit_type_;
    data.name = nullptr;
    data.used_by = MakeEmptySlice(KOOPA_RSIK_VALUE);
    data.kind.tag = KOOPA_RVT_RETURN;
    data.kind.data.ret.value = value;
    value_datas_.push_back(data);
    return &value_datas_.back();
}

const char* KoopaRawBuilder::SaveName(const std::string& name)
{
    names_.push_back(name);
    return names_.back().c_str();
}

koopa_raw_slice_t KoopaRawBuilder::MakeSlice(std::vector<const void*>& items,
    koopa_raw_slice_item_kind_t kind)
{
    koopa_raw_slice_t slice {};
    slice.buffer = items.empty() ? nullptr : const_cast<const void**>(items.data());
    slice.len = static_cast<uint32_t>(items.size());
    slice.kind = kind;
    return slice;
}

koopa_raw_slice_t KoopaRawBuilder::MakeEmptySlice(koopa_raw_slice_item_kind_t kind)
{
    koopa_raw_slice_t slice {};
    slice.buffer = nullptr;
    slice.len = 0;
    slice.kind = kind;
    return slice;
}

} // namespace koopa_ir
