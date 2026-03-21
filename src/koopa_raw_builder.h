#pragma once

// #include "../tmp/koopa.h"
#include "koopa.h"
#include "koopa_ir.h"
#include <deque>
#include <string>
#include <vector>

namespace koopa_ir {

// Converts custom Koopa IR structs to libkoopa raw program.
// Note: returned raw program references internal storage of this object.
class KoopaRawBuilder {
public:
    KoopaRawBuilder();

    koopa_raw_program_t Build(const IRProgram& program);

private:
    void Reset();

    koopa_raw_type_t LowerType(const std::string& type_name);
    koopa_raw_function_t LowerFunction(const IRFunction& function);
    koopa_raw_basic_block_t LowerBasicBlock(const IRBasicBlock& block);
    koopa_raw_value_t LowerInstruction(const IRInstruction& inst);

    koopa_raw_value_t NewInteger(int value);
    koopa_raw_value_t NewReturn(koopa_raw_value_t value);

    const char* SaveName(const std::string& name);
    static koopa_raw_slice_t MakeSlice(std::vector<const void*>& items,
        koopa_raw_slice_item_kind_t kind);
    static koopa_raw_slice_t MakeEmptySlice(koopa_raw_slice_item_kind_t kind);

    koopa_raw_type_kind_t int32_type_ {};
    koopa_raw_type_kind_t unit_type_ {};

    koopa_raw_program_t program_ {};

    std::deque<std::string> names_;

    std::deque<koopa_raw_type_kind_t> function_type_kinds_;

    std::deque<koopa_raw_value_data_t> value_datas_;
    std::deque<koopa_raw_basic_block_data_t> bb_datas_;
    std::deque<koopa_raw_function_data_t> func_datas_;

    std::vector<const void*> global_values_;
    std::vector<const void*> functions_;
    std::vector<const void*> current_blocks_;
    std::vector<const void*> current_insts_;
};

} // namespace koopa_ir
