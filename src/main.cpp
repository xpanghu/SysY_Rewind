#include "ast.h"
#include "rewind_ir.h"
#include "rewind_ir_builder.h"
#include "riscv.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace std;

// 声明 lexer 的输入，以及 parser 函数
extern FILE* yyin;
extern int yyparse(unique_ptr<BaseAST>& ast);

int main(int argc, const char* argv[])
{
    assert(argc == 5);
    auto mode = argv[1];
    auto input = argv[2];
    auto output = argv[4];

    // 打开输入文件，并且指定 lexer 在解析的时候读取这个文件
    yyin = fopen(input, "r");
    assert(yyin);

    // 调用 parser 函数，parser 函数会进一步调用 lexer 解析输入文件
    unique_ptr<BaseAST> ast;
    auto ret = yyparse(ast);
    if (ret != 0 || !ast) {
        fclose(yyin);
        throw runtime_error(
            "yyparse failed: invalid SysY input or grammar action error");
    }

    ofstream out(output);
    // 输出解析得到的 AST
    if (std::string(mode) == "-ast") {
        ast->Dump(out);
        return 0;
    }

    // ast -> rewind IR (rewind_ir 是 Koopa IR 的 C++ 形式)
    rewind_ir::RewindIRBuilder rewind_builder;
    rewind_ir::IRModule module = rewind_builder.build(*ast);

    if (std::string(mode) == "-koopa") {
        // rewind IR -> koopa IR 文本
        rewind_ir::IRTextGen ir_gen;
        auto gen_ret = ir_gen.emit(module, out);
        if (gen_ret != rewind_ir::IRErrorCode::SUCCESS) {
            fclose(yyin);
            throw runtime_error("IRTextGen::emit failed: check output file for error details");
        }
    }

    if (std::string(mode) == "-riscv") {
        // rewind_ir -> RISC-V (direct, no koopa dependency)
        riscv::emit_module(module, out);
    }

    fclose(yyin);
    return 0;
}
