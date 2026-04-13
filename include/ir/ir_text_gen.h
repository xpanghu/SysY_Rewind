#include "rewind_ir.h"
#include <fstream>
#include <sstream>

namespace rewind_ir
{
// error code
enum class IRErrorCode {
    SUCCESS = 0,
    INVALID_ARGUMENT = 1,
    GENERATION_ERROR = 2,
};

// support three output ways
// 1. emit(std::ostream&)
// 2. emit_to_string(std::string&)
// 3. emit_to_file(const std::string)
class IRTextGen
{
public:
    IRTextGen() = default;

    IRErrorCode emit(const IRModule& module, std::ostream& out);

    IRErrorCode emit_to_string(const IRModule& module, std::string& out);

    IRErrorCode emit_to_file(const IRModule& module, const std::string& file);

    std::string_view last_error() const
    {
        return last_error_;
    }

private:
    void print_global_value(const IRValue* value, std::ostream& out);
    void print_function(const IRFunction* previous, const IRFunction* current, std::ostream& out);
    void print_basic_block(const IRBasicBlock* block, std::ostream& out);
    void print_instruction(const IRValue* inst, std::ostream& out);
    void print_value(const IRValue* value, std::ostream& out);
    void print_type(const IRType* type, std::ostream& out);
    void print_binary_op(IRBinaryOp op, std::ostream& out);

    std::string last_error_;
};
} // namespace rewind_ir
