#include <iostream>
#include <memory>
#include <stdexcept>

#include "../src/ast.h"
#include "../src/koopa_ir_builder.h"
#include "../src/koopa_lib_bridge.h"
#include "../src/koopa_raw_builder.h"
#include "koopa.h"

int main()
{
    auto comp = std::make_unique<CompUnitAST>();
    auto func = std::make_unique<FuncDefAST>();
    auto type = std::make_unique<FuncTypeAST>();
    auto block = std::make_unique<BlockAST>();
    auto stmt = std::make_unique<StmtAST>();

    type->type = "int";
    func->func_type = std::move(type);
    func->ident = "main";

    stmt->number = 0;
    block->stmt = std::move(stmt);
    func->block = std::move(block);

    comp->func_def = std::move(func);

    koopa_ir::KoopaIRBuilder builder;
    const auto ir = builder.Build(*comp);

    koopa_ir::KoopaRawBuilder raw_builder;
    const auto raw_program = raw_builder.Build(ir);

    koopa_program_t koopa_program = nullptr;
    const auto ec = koopa_generate_raw_to_koopa(&raw_program, &koopa_program);
    if (ec != KOOPA_EC_SUCCESS) {
        throw std::runtime_error("koopa_generate_raw_to_koopa failed");
    }

    std::cout << koopa_ir::DumpKoopaProgramToString(koopa_program);
    koopa_delete_program(koopa_program);
    return 0;
}
