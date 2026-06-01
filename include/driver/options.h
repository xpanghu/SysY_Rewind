#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace sysy::driver
{

enum class CompileMode {
    Ast,
    Koopa,
    Ssa,
    Riscv,
};

struct CompilerOptions {
    CompileMode mode;
    std::string input_path;
    std::string output_path;
};

struct OptionParseResult {
    std::optional<CompilerOptions> options;
    int exit_code = 0;
};

std::string_view mode_name(CompileMode mode);
void print_usage(std::ostream& out, std::string_view program_name);
OptionParseResult parse_options(int argc, const char* const argv[], std::ostream& err);

} // namespace sysy::driver
