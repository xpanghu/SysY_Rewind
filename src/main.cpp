#include "ast.h"
#include "koopa.h"
#include "koopa_ir.h"
#include "koopa_ir_builder.h"
#include "riscv.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using namespace std;

// 声明 lexer 的输入, 以及 parser 函数
// 为什么不引用 sysy.tab.hpp 呢? 因为首先里面没有 yyin 的定义
// 其次, 因为这个文件不是我们自己写的, 而是被 Bison 生成出来的
// 你的代码编辑器/IDE 很可能找不到这个文件, 然后会给你报错 (虽然编译不会出错)
// 看起来会很烦人, 于是干脆采用这种看起来 dirty 但实际很有效的手段
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

    // 调用 parser 函数, parser 函数会进一步调用 lexer 解析输入文件
    unique_ptr<BaseAST> ast;
    auto ret = yyparse(ast);
    if (ret != 0 || !ast) {
        fclose(yyin);
        throw runtime_error("yyparse failed: invalid SysY input or grammar action error");
    }

    // 输出解析得到的 AST
    if (std::string(mode) == "-ast") {
        ast->Dump();
        return 0;
    }

    // 将解析得到的 AST 先转换为自定义的 koopaIR 结构，再转换为raw_program, 再转换为koopa IR程序
    koopa_ir::KoopaIRBuilder ir_builder;
    auto ir_program = ir_builder.build(*ast);

    koopa_ir::KoopaRawBuilder raw_builder;
    const auto raw_program = raw_builder.build(ir_program);
    ofstream out(output);

    if (std::string(mode) == "-koopa") {
        // 由 koopa_raw_program_t 转换为 koopa_program
        koopa_program_t koopa_program = nullptr;
        const auto ec = koopa_generate_raw_to_koopa(&raw_program, &koopa_program);
        assert(ec == KOOPA_EC_SUCCESS);

        // 由 koopa_program 转换为 koopaIR 字符串形式, 并输入到 output 文件中
        out << koopa_ir::dump_koopa_program_to_string(koopa_program);
        koopa_delete_program(koopa_program);
    } else if (std::string(mode) == "-riscv") {
        riscv::emit_program(raw_program, out);
    }

    return 0;
}