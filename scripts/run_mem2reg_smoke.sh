#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/tmp/mem2reg-smoke"
CXX="${CXX:-${HOST_CXX:-/usr/bin/clang++}}"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/mem2reg_smoke.cpp" <<'CPP'
#include "ir_text_gen.h"
#include "ir_type.h"
#include "ir_verifier.h"
#include "mem2reg.h"
#include "pass_manager.h"
#include "rewind_ir.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace rewind_ir;

namespace
{

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

bool contains_memory_form(const std::string& text)
{
    return contains(text, "alloc i32")
        || contains(text, "load ")
        || contains(text, "store ");
}

void require_contains(const std::string& text,
                      const std::string& needle,
                      const std::string& case_name)
{
    if (!contains(text, needle)) {
        std::cerr << case_name << " expected to contain: " << needle << "\n";
        std::cerr << text;
        std::exit(1);
    }
}

void require_not_contains(const std::string& text,
                          const std::string& needle,
                          const std::string& case_name)
{
    if (contains(text, needle)) {
        std::cerr << case_name << " expected not to contain: " << needle << "\n";
        std::cerr << text;
        std::exit(1);
    }
}

std::string visible_output_dir;

void write_ir_text(const std::string& name, const std::string& text)
{
    std::ofstream out(visible_output_dir + "/" + name + ".koopa");
    if (!out) {
        throw std::runtime_error("failed to write visible IR output: " + name);
    }
    out << text;
}

std::string run_mem2reg_and_print(IRModule& module, bool require_changed = true)
{
    verify_or_throw(module);
    IRPassManager pass_manager;
    pass_manager.add_module_pass(std::make_unique<Mem2RegPass>());
    const bool changed = pass_manager.run(module);
    if (require_changed && !changed) {
        throw std::runtime_error("mem2reg pass reported no changes");
    }
    verify_or_throw(module);

    IRTextGen printer;
    std::string text;
    if (printer.emit_to_string(module, text) != IRErrorCode::SUCCESS) {
        throw std::runtime_error("failed to print IR: " + std::string(printer.last_error()));
    }
    return text;
}

IRModule make_single_block()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();
    const auto* ptr_i32 = types.getPointer(i32);

    IRModule module;
    auto* function = module.make_function(types.getFunction({}, i32), "single_block");
    auto* entry = module.make_basic_block("%entry");
    module.append_basic_block(*function, *entry);

    auto* slot = module.make_value<IRAllocInst>(ptr_i32, "@x");
    auto* one = module.make_value<IRConstant>(1, i32);
    auto* store = module.make_value<IRStoreInst>(one, slot, unit);
    auto* load = module.make_value<IRLoadInst>(slot, i32, "%load");
    auto* ret = module.make_value<IRReturnInst>(load);

    module.append_value(*entry, *slot);
    module.append_value(*entry, *store);
    module.append_value(*entry, *load);
    module.append_value(*entry, *ret);
    return module;
}

IRModule make_if_else()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();
    const auto* ptr_i32 = types.getPointer(i32);

    IRModule module;
    auto* function = module.make_function(types.getFunction({}, i32), "if_else");
    auto* entry = module.make_basic_block("%entry");
    auto* then_block = module.make_basic_block("%then");
    auto* else_block = module.make_basic_block("%else");
    auto* merge = module.make_basic_block("%merge");
    module.append_basic_block(*function, *entry);
    module.append_basic_block(*function, *then_block);
    module.append_basic_block(*function, *else_block);
    module.append_basic_block(*function, *merge);

    auto* slot = module.make_value<IRAllocInst>(ptr_i32, "@x");
    auto* cond = module.make_value<IRConstant>(1, i32);
    auto* one = module.make_value<IRConstant>(1, i32);
    auto* two = module.make_value<IRConstant>(2, i32);
    module.append_value(*entry, *slot);
    module.append_value(*entry, *module.make_value<IRBranchInst>(
        cond, then_block, else_block, unit));
    module.append_value(*then_block, *module.make_value<IRStoreInst>(one, slot, unit));
    module.append_value(*then_block, *module.make_value<IRJumpInst>(merge, unit));
    module.append_value(*else_block, *module.make_value<IRStoreInst>(two, slot, unit));
    module.append_value(*else_block, *module.make_value<IRJumpInst>(merge, unit));

    auto* load = module.make_value<IRLoadInst>(slot, i32, "%load");
    module.append_value(*merge, *load);
    module.append_value(*merge, *module.make_value<IRReturnInst>(load));
    return module;
}

IRModule make_while_loop()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();
    const auto* ptr_i32 = types.getPointer(i32);

    IRModule module;
    auto* function = module.make_function(types.getFunction({}, i32), "while_loop");
    auto* entry = module.make_basic_block("%entry");
    auto* header = module.make_basic_block("%while_entry");
    auto* body = module.make_basic_block("%while_body");
    auto* exit = module.make_basic_block("%end");
    module.append_basic_block(*function, *entry);
    module.append_basic_block(*function, *header);
    module.append_basic_block(*function, *body);
    module.append_basic_block(*function, *exit);

    auto* slot = module.make_value<IRAllocInst>(ptr_i32, "@i");
    auto* zero = module.make_value<IRConstant>(0, i32);
    auto* one = module.make_value<IRConstant>(1, i32);
    auto* three = module.make_value<IRConstant>(3, i32);

    module.append_value(*entry, *slot);
    module.append_value(*entry, *module.make_value<IRStoreInst>(zero, slot, unit));
    module.append_value(*entry, *module.make_value<IRJumpInst>(header, unit));

    auto* header_load = module.make_value<IRLoadInst>(slot, i32, "%header_load");
    auto* cond = module.make_value<IRBinaryInst>(IRBinaryOp::LT, header_load, three, i32, "%cond");
    module.append_value(*header, *header_load);
    module.append_value(*header, *cond);
    module.append_value(*header, *module.make_value<IRBranchInst>(
        cond, body, exit, unit));

    auto* body_load = module.make_value<IRLoadInst>(slot, i32, "%body_load");
    auto* next = module.make_value<IRBinaryInst>(IRBinaryOp::ADD, body_load, one, i32, "%next");
    module.append_value(*body, *body_load);
    module.append_value(*body, *next);
    module.append_value(*body, *module.make_value<IRStoreInst>(next, slot, unit));
    module.append_value(*body, *module.make_value<IRJumpInst>(header, unit));

    auto* exit_load = module.make_value<IRLoadInst>(slot, i32, "%exit_load");
    module.append_value(*exit, *exit_load);
    module.append_value(*exit, *module.make_value<IRReturnInst>(exit_load));
    return module;
}

IRModule make_two_variable_if_else()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();
    const auto* ptr_i32 = types.getPointer(i32);

    IRModule module;
    auto* function = module.make_function(types.getFunction({}, i32), "two_variable_if_else");
    auto* entry = module.make_basic_block("%entry");
    auto* then_block = module.make_basic_block("%then");
    auto* else_block = module.make_basic_block("%else");
    auto* merge = module.make_basic_block("%merge");
    module.append_basic_block(*function, *entry);
    module.append_basic_block(*function, *then_block);
    module.append_basic_block(*function, *else_block);
    module.append_basic_block(*function, *merge);

    auto* x = module.make_value<IRAllocInst>(ptr_i32, "@x");
    auto* y = module.make_value<IRAllocInst>(ptr_i32, "@y");
    auto* cond = module.make_value<IRConstant>(1, i32);
    auto* one = module.make_value<IRConstant>(1, i32);
    auto* two = module.make_value<IRConstant>(2, i32);
    auto* ten = module.make_value<IRConstant>(10, i32);
    auto* twenty = module.make_value<IRConstant>(20, i32);
    module.append_value(*entry, *x);
    module.append_value(*entry, *y);
    module.append_value(*entry, *module.make_value<IRBranchInst>(
        cond, then_block, else_block, unit));

    module.append_value(*then_block, *module.make_value<IRStoreInst>(one, x, unit));
    module.append_value(*then_block, *module.make_value<IRStoreInst>(ten, y, unit));
    module.append_value(*then_block, *module.make_value<IRJumpInst>(merge, unit));

    module.append_value(*else_block, *module.make_value<IRStoreInst>(two, x, unit));
    module.append_value(*else_block, *module.make_value<IRStoreInst>(twenty, y, unit));
    module.append_value(*else_block, *module.make_value<IRJumpInst>(merge, unit));

    auto* load_x = module.make_value<IRLoadInst>(x, i32, "%load_x");
    auto* load_y = module.make_value<IRLoadInst>(y, i32, "%load_y");
    auto* sum = module.make_value<IRBinaryInst>(IRBinaryOp::ADD, load_x, load_y, i32, "%sum");
    module.append_value(*merge, *load_x);
    module.append_value(*merge, *load_y);
    module.append_value(*merge, *sum);
    module.append_value(*merge, *module.make_value<IRReturnInst>(sum));
    return module;
}

IRModule make_uninitialized_read()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* ptr_i32 = types.getPointer(i32);

    IRModule module;
    auto* function = module.make_function(types.getFunction({}, i32), "uninitialized_read");
    auto* entry = module.make_basic_block("%entry");
    module.append_basic_block(*function, *entry);

    auto* x = module.make_value<IRAllocInst>(ptr_i32, "@x");
    auto* load = module.make_value<IRLoadInst>(x, i32, "%load");
    module.append_value(*entry, *x);
    module.append_value(*entry, *load);
    module.append_value(*entry, *module.make_value<IRReturnInst>(load));
    return module;
}

IRModule make_getptr_escape()
{
    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* ptr_i32 = types.getPointer(i32);

    IRModule module;
    auto* function = module.make_function(types.getFunction({}, i32), "getptr_escape");
    auto* entry = module.make_basic_block("%entry");
    module.append_basic_block(*function, *entry);

    auto* x = module.make_value<IRAllocInst>(ptr_i32, "@x");
    auto* one = module.make_value<IRConstant>(1, i32);
    auto* zero = module.make_value<IRConstant>(0, i32);
    module.append_value(*entry, *x);
    module.append_value(*entry, *module.make_value<IRGetPtrInst>(x, one, ptr_i32, "%p"));
    module.append_value(*entry, *module.make_value<IRReturnInst>(zero));
    return module;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: mem2reg_smoke <visible-output-dir>\n";
        return 1;
    }
    visible_output_dir = argv[1];

    {
        auto module = make_single_block();
        const auto text = run_mem2reg_and_print(module);
        write_ir_text("single_block", text);
        require_not_contains(text, "alloc i32", "single block");
        require_not_contains(text, "load ", "single block");
        require_not_contains(text, "store ", "single block");
        require_contains(text, "ret 1", "single block");
    }

    {
        auto module = make_if_else();
        const auto text = run_mem2reg_and_print(module);
        write_ir_text("if_else", text);
        require_not_contains(text, "alloc i32", "if/else");
        require_not_contains(text, "load ", "if/else");
        require_not_contains(text, "store ", "if/else");
        require_contains(text, "jump %merge(1)", "if/else");
        require_contains(text, "jump %merge(2)", "if/else");
        require_contains(text, "%merge(%x_phi_0: i32):", "if/else");
        require_contains(text, "ret %x_phi_0", "if/else");
        require_not_contains(text, "mem2reg", "if/else");
    }

    {
        auto module = make_while_loop();
        const auto text = run_mem2reg_and_print(module);
        write_ir_text("while_loop", text);
        require_not_contains(text, "alloc i32", "while");
        require_not_contains(text, "load ", "while");
        require_not_contains(text, "store ", "while");
        require_contains(text, "%while_entry(%i_phi_0: i32):", "while");
        require_contains(text, "jump %while_entry(0)", "while");
        require_contains(text, "jump %while_entry(%next)", "while");
        require_contains(text, "ret %i_phi_0", "while");
        require_not_contains(text, "mem2reg", "while");
    }

    {
        auto module = make_two_variable_if_else();
        const auto text = run_mem2reg_and_print(module);
        write_ir_text("two_variable_if_else", text);
        require_not_contains(text, "alloc i32", "two variable if/else");
        require_not_contains(text, "load ", "two variable if/else");
        require_not_contains(text, "store ", "two variable if/else");
        require_contains(text, "%merge(%x_phi_0: i32, %y_phi_1: i32):",
                         "two variable if/else");
        require_contains(text, "jump %merge(1, 10)", "two variable if/else");
        require_contains(text, "jump %merge(2, 20)", "two variable if/else");
        require_contains(text, "%sum = add %x_phi_0, %y_phi_1", "two variable if/else");
        require_not_contains(text, "mem2reg", "two variable if/else");
    }

    {
        auto module = make_uninitialized_read();
        const auto text = run_mem2reg_and_print(module, false);
        write_ir_text("uninitialized_read_skipped", text);
        require_contains(text, "@x = alloc i32", "uninitialized read");
        require_contains(text, "%load = load @x", "uninitialized read");
        require_contains(text, "ret %load", "uninitialized read");
    }

    {
        auto module = make_getptr_escape();
        const auto text = run_mem2reg_and_print(module, false);
        write_ir_text("getptr_escape_skipped", text);
        require_contains(text, "@x = alloc i32", "getptr escape");
        require_contains(text, "%p = getptr @x, 1", "getptr escape");
    }

    return 0;
}
CPP

"${CXX}" -std=c++17 \
  -I"${ROOT_DIR}/include/ir" \
  "${ROOT_DIR}/src/ir/cfg_analysis.cpp" \
  "${ROOT_DIR}/src/ir/dominance_analysis.cpp" \
  "${ROOT_DIR}/src/ir/ir_rewrite.cpp" \
  "${ROOT_DIR}/src/ir/ir_text_gen.cpp" \
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${ROOT_DIR}/src/ir/ir_verifier.cpp" \
  "${ROOT_DIR}/src/ir/mem2reg.cpp" \
  "${ROOT_DIR}/src/ir/pass_manager.cpp" \
  "${OUT_DIR}/mem2reg_smoke.cpp" \
  -o "${OUT_DIR}/mem2reg_smoke"

"${OUT_DIR}/mem2reg_smoke" "${OUT_DIR}"

echo "Mem2Reg smoke output:"
echo "  ${OUT_DIR}/mem2reg_smoke"
