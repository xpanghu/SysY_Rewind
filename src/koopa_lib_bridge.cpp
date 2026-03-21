#include "koopa_lib_bridge.h"

#include <stdexcept>
#include <vector>

namespace koopa_ir {
namespace {

    std::runtime_error MakeDumpError(const char* msg, koopa_error_code_t ec)
    {
        return std::runtime_error(std::string(msg) + ", error code=" + std::to_string(static_cast<int>(ec)));
    }

    std::string DumpWithApi(koopa_program_t program,
        koopa_error_code_t (*dump_fn)(koopa_program_t, char*, size_t*))
    {
        size_t len = 0;
        auto ec = dump_fn(program, nullptr, &len);
        if (ec != KOOPA_EC_SUCCESS) {
            throw MakeDumpError("libkoopa dump length query failed", ec);
        }

        std::vector<char> buffer(len + 1, '\0');
        size_t cap = buffer.size();
        ec = dump_fn(program, buffer.data(), &cap);
        if (ec != KOOPA_EC_SUCCESS) {
            throw MakeDumpError("libkoopa dump failed", ec);
        }
        return std::string(buffer.data());
    }

} // namespace

std::string DumpKoopaProgramToString(koopa_program_t program)
{
    return DumpWithApi(program, koopa_dump_to_string);
}

std::string DumpLlvmToString(koopa_program_t program)
{
    return DumpWithApi(program, koopa_dump_llvm_to_string);
}

} // namespace koopa_ir
