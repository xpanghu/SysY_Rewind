#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
OUT_DIR="${ROOT_DIR}/tmp/machine-ir-smoke"
CXX="${CXX:-${HOST_CXX:-/usr/bin/clang++}}"

mkdir -p "${OUT_DIR}"

cmake --build "${BUILD_DIR}" -j12 -- -s

cat > "${OUT_DIR}/machine_ir_smoke.cpp" <<'CPP'
#include "machine_asm_printer.h"
#include "machine_ir.h"
#include "machine_verifier.h"
#include "instruction_selector.h"
#include "ir_type.h"
#include "rewind_ir.h"

#include <iostream>
#include <sstream>
#include <string>

using namespace riscv;
using namespace rewind_ir;

namespace
{

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

void require_contains(const std::string& text, const std::string& needle)
{
    if (!contains(text, needle)) {
        std::cerr << "expected output to contain: " << needle << "\n";
        std::cerr << text << "\n";
        std::exit(1);
    }
}

} // namespace

int main()
{
    MachineFunction function("main");
    function.frame.set_frame_size(16);
    function.frame.set_saved_ra(12);

    auto& entry = function.add_block(".Lmain_entry");
    entry.add_instr(MachineInstr::make_li(Register::t0, 42));
    entry.add_instr(MachineInstr::make_sw(Register::t0, Register::sp, 0));
    entry.add_instr(MachineInstr::make_lw(Register::a0, Register::sp, 0));
    entry.add_instr(MachineInstr::make_ret());

    MachineVerifier verifier;
    if (!verifier.verify(function)) {
        std::cerr << verifier.report() << "\n";
        return 1;
    }

    std::ostringstream out;
    MachineAsmPrinter printer(out);
    printer.emit_function(function);
    const std::string asm_text = out.str();

    require_contains(asm_text, "main:");
    require_contains(asm_text, ".Lmain_entry:");
    require_contains(asm_text, "li t0, 42");
    require_contains(asm_text, "sw t0, 0(sp)");
    require_contains(asm_text, "lw a0, 0(sp)");
    require_contains(asm_text, "ret");

    MachineFunction invalid("bad");
    invalid.add_block(".Lbad_entry");
    if (verifier.verify(invalid)) {
        std::cerr << "expected verifier to reject an unterminated machine block\n";
        return 1;
    }
    if (!contains(verifier.report(), "terminator")) {
        std::cerr << "expected verifier report to mention terminator, got:\n"
                  << verifier.report() << "\n";
        return 1;
    }

    auto& types = IRTypeContext::instance();
    const auto* i32 = types.getInt32();
    const auto* unit = types.getUnit();
    IRModule module;
    auto* ir_function = module.make_function(types.getFunction({}, i32), "selected_main");
    auto* ir_entry = module.make_basic_block("%entry");
    module.append_basic_block(*ir_function, *ir_entry);

    auto* one = module.make_value<IRConstant>(1, i32);
    auto* two = module.make_value<IRConstant>(2, i32);
    auto* sum = module.make_value<IRBinaryInst>(IRBinaryOp::ADD, one, two, i32, "%sum");
    module.append_value(*ir_entry, *sum);
    module.append_value(*ir_entry, *module.make_value<IRReturnInst>(sum));

    InstructionSelector selector;
    MachineFunction selected = selector.select_function(*ir_function);
    if (!verifier.verify(selected)) {
        std::cerr << verifier.report() << "\n";
        return 1;
    }

    std::ostringstream selected_out;
    MachineAsmPrinter selected_printer(selected_out);
    selected_printer.emit_function(selected);
    const std::string selected_asm = selected_out.str();

    require_contains(selected_asm, "selected_main:");
    require_contains(selected_asm, ".Lselected_main_entry:");
    require_contains(selected_asm, "add t0, t0, t1");
    require_contains(selected_asm, "ret");

    IRModule block_arg_module;
    auto* block_arg_function = block_arg_module.make_function(
        types.getFunction({}, i32),
        "block_arg_merge");
    auto* block_arg_entry = block_arg_module.make_basic_block("%entry");
    auto* then_block = block_arg_module.make_basic_block("%then");
    auto* else_block = block_arg_module.make_basic_block("%else");
    auto* merge_block = block_arg_module.make_basic_block("%merge");
    block_arg_module.append_basic_block(*block_arg_function, *block_arg_entry);
    block_arg_module.append_basic_block(*block_arg_function, *then_block);
    block_arg_module.append_basic_block(*block_arg_function, *else_block);
    block_arg_module.append_basic_block(*block_arg_function, *merge_block);

    auto* cond = block_arg_module.make_value<IRConstant>(1, i32);
    auto* then_value = block_arg_module.make_value<IRConstant>(10, i32);
    auto* else_value = block_arg_module.make_value<IRConstant>(20, i32);
    auto* merge_arg = block_arg_module.make_block_param(*merge_block, i32, "%x");
    block_arg_module.append_value(*block_arg_entry, *block_arg_module.make_value<IRBranchInst>(
        cond,
        then_block,
        else_block,
        unit));
    block_arg_module.append_value(*then_block, *block_arg_module.make_value<IRJumpInst>(
        merge_block,
        unit,
        std::vector<IRValue*>{then_value}));
    block_arg_module.append_value(*else_block, *block_arg_module.make_value<IRJumpInst>(
        merge_block,
        unit,
        std::vector<IRValue*>{else_value}));
    block_arg_module.append_value(*merge_block, *block_arg_module.make_value<IRReturnInst>(merge_arg));

    MachineFunction block_arg_machine = selector.select_function(*block_arg_function);
    if (!verifier.verify(block_arg_machine)) {
        std::cerr << verifier.report() << "\n";
        return 1;
    }

    std::ostringstream block_arg_out;
    MachineAsmPrinter block_arg_printer(block_arg_out);
    block_arg_printer.emit_function(block_arg_machine);
    const std::string block_arg_asm = block_arg_out.str();
    require_contains(block_arg_asm, ".Lblock_arg_merge_then:");
    require_contains(block_arg_asm, ".Lblock_arg_merge_else:");
    require_contains(block_arg_asm, ".Lblock_arg_merge_merge:");
    require_contains(block_arg_asm, "li t0, 10");
    require_contains(block_arg_asm, "li t0, 20");

    return 0;
}
CPP

"${CXX}" -std=c++17 \
  -I"${ROOT_DIR}/include/back_end" \
  -I"${ROOT_DIR}/include/ir" \
  "${ROOT_DIR}/src/back_end/asm_writer.cpp" \
  "${ROOT_DIR}/src/back_end/data_layout.cpp" \
  "${ROOT_DIR}/src/back_end/calling_convention.cpp" \
  "${ROOT_DIR}/src/back_end/frame_layout.cpp" \
  "${ROOT_DIR}/src/back_end/machine_ir.cpp" \
  "${ROOT_DIR}/src/back_end/machine_asm_printer.cpp" \
  "${ROOT_DIR}/src/back_end/machine_verifier.cpp" \
  "${ROOT_DIR}/src/back_end/instruction_selector.cpp" \
  "${ROOT_DIR}/src/ir/ir_type.cpp" \
  "${OUT_DIR}/machine_ir_smoke.cpp" \
  -o "${OUT_DIR}/machine_ir_smoke"

"${OUT_DIR}/machine_ir_smoke"

echo "Machine IR smoke output:"
echo "  ${OUT_DIR}/machine_ir_smoke"
