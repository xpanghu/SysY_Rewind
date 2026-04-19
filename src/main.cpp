#include "ast.h"
#include "rewind_ir.h"
#include "ir_builder.h"
#include "ir_text_gen.h"
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

    yyin = fopen(input, "r");
    assert(yyin);

    unique_ptr<BaseAST> ast;
    auto ret = yyparse(ast);
    if (ret != 0 || !ast) {
        fclose(yyin);
        throw runtime_error("yyparse failed: invalid SysY input or grammar action error");
    }

    ofstream out(output);

    // print AST
    if (std::string(mode) == "-ast") {
        ast->Dump(out);
        return 0;
    }

    // ast -> IR
    rewind_ir::RewindIRBuilder rewind_builder;
    rewind_ir::IRModule module = rewind_builder.build(*ast);

    if (std::string(mode) == "-koopa") {
        // gen IR text
        rewind_ir::IRTextGen ir_gen;
        auto gen_ret = ir_gen.emit(module, out);
        if (gen_ret != rewind_ir::IRErrorCode::SUCCESS) {
            fclose(yyin);
            throw runtime_error("IRTextGen::emit failed: check output file for error details");
        }
    }

    if (std::string(mode) == "-riscv") {
        // IR -> RISC-V (direct)
        riscv::emit_module(module, out);
    }

    fclose(yyin);
    return 0;
}
