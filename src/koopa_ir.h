#pragma once

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

} // namespace koopa_ir
