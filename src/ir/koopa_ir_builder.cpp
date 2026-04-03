#include "ir/koopa_ir_builder.h"
#include "ir/koopa.h"

namespace rewind_ir {
// 这一部分只需要顺序遍历 koopa_ir 就可以得到 koopa_raw_program_t
KoopaRawBuilder::KoopaRawBuilder()
{
    int32_type_.tag = KOOPA_RTT_INT32;
    unit_type_.tag = KOOPA_RTT_UNIT;
    reset();
}

koopa_raw_program_t KoopaRawBuilder::build(const IRModule& module)
{
    reset();
    return koopa_raw_program_t {};
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

// 获取操作运算符
koopa_raw_binary_op_t KoopaRawBuilder::lower_binary_op(IRBinaryOp op)
{
    switch (op) {
    case IRBinaryOp::ADD:
        return KOOPA_RBO_ADD;
    case IRBinaryOp::SUB:
        return KOOPA_RBO_SUB;
    case IRBinaryOp::MUL:
        return KOOPA_RBO_MUL;
    case IRBinaryOp::DIV:
        return KOOPA_RBO_DIV;
    case IRBinaryOp::MOD:
        return KOOPA_RBO_MOD;
    case IRBinaryOp::OR:
        return KOOPA_RBO_OR;
    case IRBinaryOp::AND:
        return KOOPA_RBO_AND;
    case IRBinaryOp::LT:
        return KOOPA_RBO_LT;
    case IRBinaryOp::GT:
        return KOOPA_RBO_GT;
    case IRBinaryOp::LE:
        return KOOPA_RBO_LE;
    case IRBinaryOp::GE:
        return KOOPA_RBO_GE;
    case IRBinaryOp::EQ:
        return KOOPA_RBO_EQ;
    case IRBinaryOp::NEQ:
        return KOOPA_RBO_NOT_EQ;
    case IRBinaryOp::SAR:
        return KOOPA_RBO_SAR;
    case IRBinaryOp::SHL:
        return KOOPA_RBO_SHL;
    case IRBinaryOp::SHR:
        return KOOPA_RBO_SHR;
    case IRBinaryOp::XOR:
        return KOOPA_RBO_XOR;
    default:
        break;
    }
    throw std::runtime_error("unsupported IR binary op");
}

// 对应 koopa_raw_value_tag_t 中 KOOPA_RSIK_VALUE
// 构造值对象,类型是i32
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

// 对应 koopa_raw_value_tag_t 中 KOOPA_RSIV_BINARY
// 构造二元运算,ty是i32, name是当前操作结果的寄存器名
koopa_raw_value_t KoopaRawBuilder::new_binary(koopa_raw_binary_op_t op,
    koopa_raw_value_t lhs, koopa_raw_value_t rhs,
    const std::string& name)
{
    koopa_raw_value_data_t data {};
    data.ty = &int32_type_;
    data.name = save_name(name);
    data.used_by = make_empty_slice(KOOPA_RSIK_VALUE);
    data.kind.tag = KOOPA_RVT_BINARY;
    data.kind.data.binary.op = op;
    data.kind.data.binary.lhs = lhs;
    data.kind.data.binary.rhs = rhs;
    value_datas_.push_back(data);
    return &value_datas_.back();
}

// 对应 koopa_raw_value_tag_t 中 KOOPA_RVT_RETURN
// 构造返回指令, 类型是unit
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
