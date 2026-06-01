#include "pipeline.h"
#include "ast.h"
#include "ir_builder.h"
#include "ir_text_gen.h"
#include "ir_verifier.h"
#include "mem2reg.h"
#include "pass_manager.h"
#include "riscv.h"
#include <cstdio>
#include <exception>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

extern FILE* yyin;
extern int yyparse(std::unique_ptr<BaseAST>& ast);

namespace sysy::driver
{
namespace
{

class FileHandle
{
public:
    explicit FileHandle(FILE* file) : file_(file)
    {
    }

    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    ~FileHandle()
    {
        if (file_ != nullptr) {
            fclose(file_);
        }
    }

    FILE* get() const
    {
        return file_;
    }

private:
    FILE* file_ = nullptr;
};

class YyinGuard
{
public:
    explicit YyinGuard(FILE* input) : previous_(yyin)
    {
        yyin = input;
    }

    YyinGuard(const YyinGuard&) = delete;
    YyinGuard& operator=(const YyinGuard&) = delete;

    ~YyinGuard()
    {
        yyin = previous_;
    }

private:
    FILE* previous_ = nullptr;
};

std::unique_ptr<BaseAST> parse_ast(const std::string& input_path)
{
    FileHandle input(fopen(input_path.c_str(), "r"));
    if (input.get() == nullptr) {
        throw std::runtime_error("failed to open input file: " + input_path);
    }

    YyinGuard yyin_guard(input.get());
    std::unique_ptr<BaseAST> ast;
    const int ret = yyparse(ast);

    if (ret != 0 || !ast) {
        throw std::runtime_error("yyparse failed: invalid SysY input or grammar action error");
    }
    return ast;
}

std::ofstream open_output(const std::string& output_path)
{
    std::ofstream out(output_path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + output_path);
    }
    return out;
}

} // namespace

int run_compiler(const CompilerOptions& options, std::ostream&)
{
    auto ast = parse_ast(options.input_path);
    auto out = open_output(options.output_path);

    if (options.mode == CompileMode::Ast) {
        ast->Dump(out);
        return 0;
    }

    rewind_ir::RewindIRBuilder rewind_builder;
    rewind_ir::IRModule module = rewind_builder.build(*ast);
    rewind_ir::verify_or_throw(module);

    rewind_ir::IRPassManager pass_manager;
    pass_manager.add_module_pass(std::make_unique<rewind_ir::IRNoOpModulePass>());
    if (options.mode == CompileMode::Ssa) {
        pass_manager.add_module_pass(std::make_unique<rewind_ir::Mem2RegPass>());
    }
    pass_manager.run(module);

    rewind_ir::verify_or_throw(module);

    switch (options.mode) {
    case CompileMode::Ast:
        break;
    case CompileMode::Koopa: {
        rewind_ir::IRTextGen ir_gen;
        const auto gen_ret = ir_gen.emit(module, out);
        if (gen_ret != rewind_ir::IRErrorCode::SUCCESS) {
            throw std::runtime_error("IRTextGen::emit failed: " + std::string(ir_gen.last_error()));
        }
        return 0;
    }
    case CompileMode::Ssa: {
        rewind_ir::IRTextGen ir_gen;
        const auto gen_ret = ir_gen.emit(module, out);
        if (gen_ret != rewind_ir::IRErrorCode::SUCCESS) {
            throw std::runtime_error("IRTextGen::emit failed: " + std::string(ir_gen.last_error()));
        }
        return 0;
    }
    case CompileMode::Riscv:
        riscv::emit_module(module, out);
        return 0;
    }

    throw std::runtime_error("unsupported compile mode: " + std::string(mode_name(options.mode)));
}

int run(int argc, const char* const argv[], std::ostream& err)
{
    const auto parse_result = parse_options(argc, argv, err);
    if (!parse_result.options.has_value()) {
        return parse_result.exit_code;
    }

    try {
        return run_compiler(*parse_result.options, err);
    } catch (const std::exception& ex) {
        err << "error: " << ex.what() << "\n";
        return 1;
    }
}

} // namespace sysy::driver
