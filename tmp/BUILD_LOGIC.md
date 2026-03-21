IR construction plan for current SysY stage

Goal
- Keep parser/AST code as-is.
- Add a middle layer: AST -> in-memory Koopa IR model -> raw program -> libkoopa output.

Files in this tmp directory
- koopa_ir.h: IR data structures (instruction/basic block/function/program).
- koopa_ir_builder.h/.cpp: Lowering from AST to IR.
- koopa_raw_builder.h/.cpp: Convert custom IRProgram to koopa_raw_program_t.
- koopa_lib_bridge.h/.cpp: Wrap libkoopa dump-to-string APIs.
- ir_smoke_test.cpp: Standalone smoke test using official libkoopa output path.
- koopa_ir_printer.h/.cpp: Debug fallback printer (deprecated).

Current supported SysY subset
- Program shape: int main() { return Number; }
- AST mapping:
  - CompUnitAST -> IRProgram
  - FuncDefAST  -> IRFunction
  - BlockAST    -> one entry basic block
  - StmtAST(number) -> ret <number>

Build and run the standalone smoke test
1) Compile
  clang++ -std=c++17 -I./src -I./tmp ./tmp/ir_smoke_test.cpp ./tmp/koopa_ir_builder.cpp ./tmp/koopa_raw_builder.cpp ./tmp/koopa_lib_bridge.cpp -L"${CDE_LIBRARY_PATH}/native" -lkoopa -o ./tmp/ir_smoke_test

2) Run
   ./tmp/ir_smoke_test

Expected output
fun @main(): i32 {
%entry:
  ret 0
}

How to integrate into existing compiler (official path)
1) Copy these files from tmp/ to src/ (or change Makefile SRCS to include tmp/*.cpp).
2) In main.cpp, after yyparse(ast):
   - mode == "-ast": keep ast->Dump().
   - mode == "-koopa":
     - KoopaIRBuilder builder;
     - IRProgram program = builder.Build(*ast);
  - KoopaRawBuilder raw_builder;
  - koopa_raw_program_t raw = raw_builder.Build(program);
  - koopa_generate_raw_to_koopa(&raw, &koopa_program);
  - std::string text = DumpKoopaProgramToString(koopa_program);
  - koopa_delete_program(koopa_program);
     - write text to output file path.
3) Keep existing parser/flex/bison pipeline unchanged.

Why this split helps
- AST remains syntax-focused.
- IR struct is backend-facing and easier to optimize/transform later.
- Official output is delegated to libkoopa, reducing handwritten serialization bugs.
- Deprecated printer can still be used for quick debug snapshots.

Raw program bridge (new step)
- koopa_raw_builder.h/.cpp converts custom IRProgram to koopa_raw_program_t.
- koopa_lib_bridge.h/.cpp wraps libkoopa dump-to-string APIs.
- raw_bridge_smoke_test.cpp demonstrates:
  1) AST -> IRProgram
  2) IRProgram -> raw program
  3) koopa_generate_raw_to_koopa(raw) -> koopa_program_t
  4) dump koopa text and LLVM text via libkoopa

Build and run raw bridge demo
1) Compile (example with existing Makefile env variables)
  clang++ -std=c++17 -I./tmp -I./src \
    ./tmp/raw_bridge_smoke_test.cpp \
    ./tmp/koopa_ir_builder.cpp \
    ./tmp/koopa_raw_builder.cpp \
    ./tmp/koopa_lib_bridge.cpp \
    -L"${CDE_LIBRARY_PATH}/native" -lkoopa -o ./tmp/raw_bridge_smoke_test

2) Run
  ./tmp/raw_bridge_smoke_test

Integration into current compiler
1) After building custom IRProgram, call KoopaRawBuilder::Build.
2) Call koopa_generate_raw_to_koopa to get koopa_program_t.
3) Depending on output mode:
  - koopa_dump_to_string / koopa_dump_to_file for Koopa IR text
  - koopa_dump_llvm_to_string / koopa_dump_llvm_to_file for LLVM IR
4) Always call koopa_delete_program after using koopa_program_t.
