# Architecture Task Progress

这份文档只记录架构重构的推进顺序和执行记录。

文档分工：

- `target_architecture_and_tasks.md` 记录 A1-A13 的长期方向和 overview。
- `riscv_backend_design.md`、`ssa_mem2reg_design.md` 等专项文档记录具体方案和实施细节。
- 本文档记录“下一步做什么”和“已经完成了什么”，避免把执行日志塞回 overview 文档。

## 当前推进顺序

已完成的主线整理：

1. [x] A1 头文件解耦。
2. [x] A2 Driver 独立化。
3. [x] A0.2 重构支持边界表。
4. [x] A5.1 RuntimeDecls 拆分。
5. [x] A5.2 ConstEvaluator 拆分。
6. [x] A7.1 AsmWriter 拆分。
7. [x] A7.2 FrameLayout 拆分。
8. [x] A7.3 CallingConvention 拆分。
9. [x] A7.4 target DataLayout 拆分。
10. [x] A7.5 后端支持表。
11. [x] A5.3 数组初始化 helper 拆分。
12. [x] A5.4 表达式 lowering 拆分。
13. [x] A5.5 语句和控制流 lowering 拆分。
14. [x] A9 测试和回归保护。
15. [x] A5.6 语义检查和 lowering 解耦，第一阶段。
16. [x] A10 IR Verifier 和 Pass Manager，第一阶段。
17. [x] A11.1 IR 支持 block arguments，第一阶段。
18. [x] A11.2 CFGAnalysis，第一阶段。
19. [x] A11.3 DominanceAnalysis，第一阶段。
20. [x] A11.4 IR rewrite 基础设施，第一阶段。
21. [x] A11.5 标量 mem2reg，第一阶段。
22. [x] A11.5 补充 `-ssa` 真实前端链路验证入口。
23. [x] A13.0 Machine IR 文档和边界同步。
24. [x] A13.1 Machine IR core。
25. [x] A13.2 InstructionSelector 平铺和映射。
26. [x] A13.3 block argument edge-copy / parallel-copy。
27. [x] A13.4 MachineVerifier / MachinePrinter。
28. [x] A13.5 stack-backed frame layout。
29. [x] A13.6 prologue / epilogue insertion。
30. [x] A13.7 MachineAsmPrinter -> AsmWriter -> RISC-V asm。
31. [x] A13.8 Machine IR smoke / RISC-V smoke。

后续建议推进：

1. [ ] A12 基础 IR 优化 Pass。
2. [ ] A13 后续寄存器分配、peephole、block placement、instruction scheduling。
3. [ ] 逐步清理 `riscv.cpp` 中历史 `IREmitter`，避免长期保留两套后端实现。

推进原则：

- 先补基础设施，再启用行为变化。
- 每个阶段都要保留 `-koopa`、`-riscv` 或 smoke 级别的最小验证路径。
- A11 停在 SSA/mem2reg；Machine IR、汇编输出和寄存器分配统一归入 A13，但 A13 内部必须分阶段推进，不能一次性混做。

## 执行记录

- 2026-04-25: 完成 A1 第一阶段。`include/front_end/ast.h` 不再依赖 IR，`include/ir/symbol_table.h` 不再依赖 AST，`include/ir/rewind_ir.h` 中无用 AST 前向声明已移除。
- 2026-04-25: 完成 A2 第一阶段。新增 `include/driver` 与 `src/driver`，`main.cpp` 只调用 `sysy::driver::run`，命令行解析和编译流程编排移动到 driver 层。
- 2026-04-25: 完成 A8 的一部分。baremetal 构建、链接和 QEMU 运行逻辑迁移到 `Makefile`，脚本只保留兼容包装器；最终产物输出到 `riscv32-baremetal/<input-name>/`。
- 2026-05-03: 完成 A0.2。`docs/architecture/support_matrix.md` 改为重构支持边界表，用来记录 SysY 能力跨 parser/AST、lowering、IR text、RISC-V/baremetal 的影响面和回归锚点。
- 2026-05-03: 完成 A5.1 第一阶段。SysY runtime 声明从 `src/ir/ir_builder.cpp` 拆到 `src/ir/ir_builder_runtime_decls.cpp`，`RewindIRBuilder::declare_library_function` 只保留兼容入口。
- 2026-05-03: 完成 A5.2 第一阶段。常量求值主体从 `src/ir/ir_builder.cpp` 拆到 `src/ir/ir_builder_const_eval.cpp` 的 `ConstEvaluator`，`RewindIRBuilder::eval_exp` 作为现有 lowering 调用点的兼容入口。
- 2026-05-06: 补充主线重构之后的能力建设路线：A10 IR Verifier 和 Pass Manager、A11 SSA 和 mem2reg、A12 基础 IR 优化 Pass、A13 RISC-V Machine IR 和寄存器分配优化。A10-A13 当前只作为长期路线合并记录，后续每项都应拆成独立任务文档；MLIR bridge 不放入当前项目路线。
- 2026-05-18: 完成 A7.1。新增 `AsmWriter`，将 RISC-V 原始汇编文本输出从 `riscv.cpp` 中拆出，`IREmitter` 保留 IR 指令调度和后端遍历职责。
- 2026-05-18: 调整任务文档结构。将推荐推进顺序移到文档开头并标记完成状态；移除“IR 文本输出整理”独立任务，明确 `-koopa` 当前就是自定义 IR 文本输出入口。
- 2026-05-18: 完成 A7.2-A7.5 第一阶段。新增 `FrameLayout`、`CallingConvention`、`DataLayout` 和 `docs/architecture/riscv_backend_design.md`，将栈帧布局、调用约定、目标数据大小和后端支持边界从 `riscv.cpp` 中拆出。
- 2026-05-18: 完成 A5.3-A5.5 第一阶段。数组初始化逻辑保留在 `src/ir/ir_builder_array_init.cpp`，表达式/lvalue/call lowering 移动到 `src/ir/ir_builder_expr.cpp`，语句和控制流 lowering 移动到 `src/ir/ir_builder_stmt.cpp`，共享 helper 收敛到 `src/ir/ir_builder_internal.h`。
- 2026-05-18: 完成 A9 第一阶段。新增 `scripts/run_regression_smoke.sh` 和 `make regression-smoke`，提供本地构建、hello 的 `-koopa/-riscv`、awesome-sysy `lisp.c` 的 RISC-V 生成，以及可选 Docker lv9 autotest 入口。
- 2026-05-20: 补充 A5.6 语义检查和 lowering 解耦任务。明确后续推进顺序为 A5.6 -> A10 -> A11 -> A13 -> A12：先把源语言语义边界从 lowering 中逐步拆出，再建立 IR Verifier、PassManager 和 SSA/mem2reg 验证入口；随后先补后端 Machine IR 承接层，最后再推进基础 IR 优化。
- 2026-05-20: 完成 A5.6 第一阶段。新增 `include/ir/semantic_checks.h` 和 `src/ir/semantic_checks.cpp`，以 `VariableSemanticInfo` 承接部分符号解析结果，并将函数调用、左值可变性、数组下标数量、return、break/continue 等高频语义判断从 lowering 主流程中抽出；新增 `scripts/run_semantic_smoke.sh` 和 `make semantic-smoke` 验证合法/非法语义边界。
- 2026-05-20: 完成 A10 第一阶段。新增 `IRVerifier`、`IRPassManager` 和 `IRNoOpModulePass`，driver 在 `-koopa`/`-riscv` 输出前执行 `lower -> verify -> no-op pass -> verify`；新增 `scripts/run_ir_verifier_smoke.sh` 和 `make ir-verifier-smoke`，覆盖合法 IR、错误 IR 和 no-op pass 不改变 IR 的最小闭环。
- 2026-05-21: 推进 A8/A9 目录整理。新增 `docs/architecture/test_artifact_layout.md` 固定 `tests/`、`scripts/`、`tmp/`、`riscv32-baremetal/` 的职责；`tests/` 改为版本化测试资产，基础 smoke 输入迁到 `tests/smoke/`，`awesome-sysy` 稳定输入 fixture 迁到 `tests/fixtures/awesome-sysy/`，手工 `-ast/-koopa/-riscv` 输出统一进入 `tmp/manual/`。
- 2026-05-22: 为 A11 新增 `docs/architecture/ssa_mem2reg_design.md`。A11 的 SSA 合流表示确定为 basic block arguments 与控制流边参数，传统 Phi 只保留等价语义对照；A11 按 IR 表示、CFG/dominance、IR rewrite、标量 mem2reg 的顺序推进，后端 Machine IR 承接层统一归入 A13。
- 2026-05-22: 完成 A11.1 第一阶段。Rewind IR 新增 block argument value、basic block parameter 列表和 branch/jump edge args；IR text 与 verifier 支持 block params 和 edge arg 数量/类型检查；`FuncContext` 兼容新构造接口但当前 SysY lowering 仍默认传空 edge args。
- 2026-05-27: 完成 A11.1 补强任务。`IRBlockArgRef` 增加 `owner_`，block argument 创建入口收敛到 `IRModule::make_block_param`，禁止通过 `make_value<IRBlockArgRef>` 绕过 owner/index 维护；`IRVerifier` 增加 owner 一致性检查，`run_ir_verifier_smoke.sh` 增加 owner 断言。验证：`make ir-verifier-smoke`、`make regression-smoke`、相关文件 `git diff --check`。
- 2026-05-27: 完成 A11.2 第一阶段。新增 `CFGAnalysis`，从 block terminator 推导 successors，反向建立 predecessors，并提供 entry、function block 顺序、reachable blocks、`has_edge` 查询；新增 `scripts/run_cfg_analysis_smoke.sh` 和 `make cfg-analysis-smoke`，覆盖 `if/else` 合流、不可达 block 和 `while` 回边。验证：先观察 RED，再通过 `make cfg-analysis-smoke`。
- 2026-05-27: 完成 A11.3 第一阶段。新增 `DominanceAnalysis`，基于 `CFGAnalysis` 计算 reachable blocks 上的 dominator sets、immediate dominator、dominator tree children 和 dominance frontier；新增 `scripts/run_dominance_analysis_smoke.sh` 和 `make dominance-analysis-smoke`，覆盖 `if/else` merge frontier、不可达 block idom 和 `while` header/self frontier。验证：先观察 RED，再通过 `make dominance-analysis-smoke`。
- 2026-05-27: 完成 A11.4 第一阶段。新增 `ir_rewrite` 工具层，统一遍历并替换 instruction operands，支持 function/module 级 use 替换，并提供 basic block instruction erase helper；死 value 仍由 `IRModule` 持有，pass 只从 block 指令序列中摘除。新增 `scripts/run_ir_rewrite_smoke.sh` 和 `make ir-rewrite-smoke`，验证 `load` use 替换、旧 `load` 删除、printer/verifier 闭环以及 edge args operand 替换。验证：先观察 RED，再通过 `make ir-rewrite-smoke`。
- 2026-05-27: 完成 A11.5 第一阶段。新增 `Mem2RegPass`，以可选 `IRModulePass` 形式接入 `IRPassManager`，第一版只提升局部、非逃逸、直接 load/store 使用的标量 `alloc i32`，并保守跳过可能读取未初始化值或涉及数组/指针逃逸的 alloc；pass 使用 dominance frontier 添加 block arguments，沿 dominator tree 重命名，用 A11.4 rewrite 接口替换 load use 并删除 promoted `alloc/load/store`。新增 `scripts/run_mem2reg_smoke.sh` 和 `make mem2reg-smoke`，覆盖 single block、`if/else` merge、`while` backedge、双变量合流、未初始化读取跳过和 `getptr` 派生地址跳过。当前 driver 默认仍不启用 mem2reg，避免在 RISC-V backend 尚未支持 block arguments 前破坏主链路。验证：先观察 RED，再通过 `make mem2reg-smoke`；可视化 IR 输出位于 `tmp/mem2reg-smoke/*.koopa`。
- 2026-05-28: 为 A11.5 补充真实前端链路验证入口。新增 `-ssa` compile mode，真实流程为 `SysY source -> AST -> memory-form IR -> Mem2RegPass -> SSA IR text`；新增 `scripts/run_ssa_smoke.sh` 和 `make ssa-smoke`，覆盖 `if/else` 合流与 `while` 回边，并检查 promoted 标量变量不再残留 `alloc i32` / `load` / `store`。`-koopa` 和 `-riscv` 默认链路不启用 mem2reg。
- 2026-06-01: 统一后端任务边界。A11 明确停在 SSA/mem2reg 和 `-ssa` 验证；Machine IR、InstructionSelector、stack-backed frame layout、prologue/epilogue、MachineAsmPrinter、AsmWriter 到 RISC-V asm 的链路统一归入 A13。A13 第一阶段目标是可运行的 Machine IR MVP，寄存器分配、peephole、block placement 和 instruction scheduling 作为后续阶段。
- 2026-06-01: 完成 A13 第一阶段。新增 Machine IR core、`MachineFrame`、`InstructionSelector`、`MachineVerifier`、`MachineAsmPrinter` 和 `machine-ir-smoke`；默认 `-riscv` 改为 `Rewind IR -> InstructionSelector -> Machine IR -> MachineVerifier -> MachineAsmPrinter -> AsmWriter -> RISC-V asm`。第一阶段仍采用 stack-backed 策略，长期值保存在栈槽中，scratch physical registers 只用于 materialize。验证：`make machine-ir-smoke`、`make regression-smoke`、`docker run -i --rm -v /Users/qingxuliang/project/compiler/SysY:/root/compiler maxxing/compiler-dev autotest -riscv -s perf /root/compiler`。
