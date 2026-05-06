#pragma once

#include "options.h"
#include <iosfwd>

namespace sysy::driver
{

int run_compiler(const CompilerOptions& options, std::ostream& err);
int run(int argc, const char* const argv[], std::ostream& err);

} // namespace sysy::driver

