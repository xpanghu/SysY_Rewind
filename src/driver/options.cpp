#include "options.h"
#include <ostream>
#include <string_view>
#include <utility>

namespace sysy::driver
{
namespace
{

std::optional<CompileMode> parse_mode(std::string_view mode)
{
    if (mode == "-ast") {
        return CompileMode::Ast;
    }
    if (mode == "-koopa") {
        return CompileMode::Koopa;
    }
    if (mode == "-ssa") {
        return CompileMode::Ssa;
    }
    if (mode == "-riscv") {
        return CompileMode::Riscv;
    }
    return std::nullopt;
}

bool is_help(std::string_view arg)
{
    return arg == "-h" || arg == "--help";
}

} // namespace

std::string_view mode_name(CompileMode mode)
{
    switch (mode) {
    case CompileMode::Ast:
        return "-ast";
    case CompileMode::Koopa:
        return "-koopa";
    case CompileMode::Ssa:
        return "-ssa";
    case CompileMode::Riscv:
        return "-riscv";
    }
    return "<unknown>";
}

void print_usage(std::ostream& out, std::string_view program_name)
{
    out << "Usage: " << program_name << " <-ast|-koopa|-ssa|-riscv> <input.sysy> -o <output>\n";
}

OptionParseResult parse_options(int argc, const char* const argv[], std::ostream& err)
{
    const std::string_view program_name = argc > 0 ? argv[0] : "compiler";
    if (argc == 2 && is_help(argv[1])) {
        print_usage(err, program_name);
        return {{}, 0};
    }

    if (argc != 5) {
        print_usage(err, program_name);
        return {{}, 1};
    }

    auto mode = parse_mode(argv[1]);
    if (!mode.has_value()) {
        err << "error: unknown compile mode: " << argv[1] << "\n";
        print_usage(err, program_name);
        return {{}, 1};
    }

    if (std::string_view(argv[3]) != "-o") {
        err << "error: expected '-o' before output path\n";
        print_usage(err, program_name);
        return {{}, 1};
    }

    CompilerOptions options{
        *mode,
        argv[2],
        argv[4],
    };
    return {std::move(options), 0};
}

} // namespace sysy::driver
