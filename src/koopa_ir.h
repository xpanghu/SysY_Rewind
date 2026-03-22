#pragma once

#include "../tmp/koopa.h"

#include <deque>
#include <string>
#include <vector>

namespace koopa_ir {

struct IRInstruction {
    enum class Kind {
        kRet,
    };

    Kind kind;
    int ret_value = 0;
};

struct IRBasicBlock {
    std::string name;
    std::vector<IRInstruction> insts;
};

struct IRFunction {
    std::string name;
    std::string ret_type = "i32";
    std::vector<IRBasicBlock> blocks;
};

struct IRProgram {
    std::vector<IRFunction> functions;
};

// Exports custom IR structs to libkoopa raw program and provides dump helpers.
// Returned raw program references this object's internal storage.
class KoopaRawBuilder {
public:
    KoopaRawBuilder();

    koopa_raw_program_t build(const IRProgram& program);

private:
    void reset();

    koopa_raw_type_t lower_type(const std::string& type_name);
    koopa_raw_function_t lower_function(const IRFunction& function);
    koopa_raw_basic_block_t lower_basic_block(const IRBasicBlock& block);
    koopa_raw_value_t lower_instruction(const IRInstruction& inst);

    koopa_raw_value_t new_integer(int value);
    koopa_raw_value_t new_return(koopa_raw_value_t value);

    const char* save_name(const std::string& name);
    static koopa_raw_slice_t make_slice(std::vector<const void*>& items,
        koopa_raw_slice_item_kind_t kind);
    static koopa_raw_slice_t make_empty_slice(koopa_raw_slice_item_kind_t kind);

    koopa_raw_type_kind_t int32_type_ {};
    koopa_raw_type_kind_t unit_type_ {};
    koopa_raw_type_kind_t array_type_ {};
    koopa_raw_type_kind_t pointer_type_ {};

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

std::string dump_koopa_program_to_string(koopa_program_t program);
} // namespace koopa_ir
