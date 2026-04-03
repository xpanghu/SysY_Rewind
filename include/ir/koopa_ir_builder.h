#include "koopa.h"
#include "rewind_ir.h"
namespace rewind_ir {
class KoopaRawBuilder {
public:
    KoopaRawBuilder();

    koopa_raw_program_t build(const IRModule& module);

private:
    void reset();

    koopa_raw_type_t lower_type(const std::string& type_name);
    koopa_raw_function_t lower_function(const IRFunction& function);
    koopa_raw_basic_block_t lower_basic_block(const IRBasicBlock& block);
    koopa_raw_value_t lower_instruction(const IRInstruction& inst);
    koopa_raw_value_t lower_ir_value(const IRValue& value);
    koopa_raw_binary_op_t lower_binary_op(IRBinaryOp op);

    koopa_raw_value_t make_binary(const IRInstruction& inst);
    koopa_raw_value_t new_integer(int value);
    koopa_raw_value_t new_binary(koopa_raw_binary_op_t op,
        koopa_raw_value_t lhs, koopa_raw_value_t rhs,
        const std::string& name);
    koopa_raw_value_t new_return(koopa_raw_value_t value);
    std::string to_raw_specific_name(const std::string& name);
    std::string to_raw_temp_name(const std::string& name);

    const char* save_name(const std::string& name);
    static koopa_raw_slice_t make_slice(std::vector<const void*>& items,
        koopa_raw_slice_item_kind_t kind);
    static koopa_raw_slice_t make_empty_slice(koopa_raw_slice_item_kind_t kind);

    koopa_raw_type_kind_t int32_type_ {};
    koopa_raw_type_kind_t unit_type_ {};

    koopa_raw_program_t program_ {};

    // koopa_raw_program_t 中数据都是指针类型，通过容器将这些数据存储起来
    std::deque<std::string> names_;
    std::deque<koopa_raw_type_kind_t> function_type_kinds_;
    std::deque<koopa_raw_value_data_t> value_datas_;
    std::deque<koopa_raw_basic_block_data_t> bb_datas_;
    std::deque<koopa_raw_function_data_t> func_datas_;

    // 临时存储的 functions, blocks, insts等, 用于保存遍历过程中的数据
    std::vector<const void*> global_values_;
    std::vector<const void*> functions_;
    std::vector<const void*> current_blocks_;
    std::vector<const void*> current_insts_;
};

std::string dump_koopa_program_to_string(koopa_program_t program);
}