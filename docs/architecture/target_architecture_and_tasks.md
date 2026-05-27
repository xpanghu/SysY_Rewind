# Target Architecture And Task Breakdown

这份文档给出项目的优化架构方向和长期任务拆分。每个任务都应尽量小，并保持当前可运行链路。

## 架构原则

1. 前端不依赖 IR。
2. IR core 不依赖 AST。
3. 后端只依赖稳定 IR 接口，不依赖 AST 或 parser。
4. driver 负责阶段编排，不承担具体编译逻辑。
5. 语义分析、常量求值、数组初始化、控制流 lowering 可以共享上下文，但不应全部堆在一个大类里。
6. 每次重构都要保留 `-ast`、`-koopa`、`-riscv` 的最小验证路径。

## 文档分工

本文档只记录项目长期方向、目标结构和 A1-A13 的 overview。

推进顺序和执行记录见 `docs/architecture/task_progress.md`。具体专项设计见对应任务文档，例如：

- `docs/architecture/riscv_backend_design.md`: RISC-V 后端结构、支持边界和 Machine IR 演进方向。
- `docs/architecture/ssa_mem2reg_design.md`: A11 SSA、block arguments、CFG/dominance、mem2reg 的具体方案。
- `docs/architecture/test_artifact_layout.md`: 测试资产、临时产物和 baremetal 产物目录边界。

说明：`-koopa` 是当前自定义 IR 文本形式的输出入口，名称来自早期实践和历史命名，不再把它视为一个需要单独辨析“自定义 IR 文本”或“Koopa 兼容文本”的任务。MLIR bridge 暂不放入本项目任务路线；如果后续要做，更适合作为新的 AI compiler 项目单独设计。

## 建议目标结构

这是长期目标结构，不要求一次完成。

```text
include/
  front_end/
    ast.h
    parser.h
    ast_printer.h
  semantic/
    symbol_table.h
    const_eval.h
  ir/
    type.h
    value.h
    instruction.h
    module.h
    builder.h
    verifier.h
    text_printer.h
  lower/
    ast_to_ir.h
    lowering_context.h
    array_init_lowering.h
    control_flow_lowering.h
    runtime_decls.h
  back_end/
    riscv/
      asm_writer.h
      frame_layout.h
      calling_convention.h
      instruction_emitter.h
      backend.h
  driver/
    options.h
    pipeline.h
    diagnostics.h

src/
  front_end/
  semantic/
  ir/
  lower/
  back_end/riscv/
  driver/
```

短期可以先不移动目录，只在现有目录内拆分文件和接口。等边界稳定后，再考虑目录重排。

## 模块交互

目标编译链：

```text
Driver
  -> FrontEnd::parse(source) -> AST
  -> Lower::build_ir(AST, options) -> IRModule
  -> optional IRVerifier
  -> mode dispatch:
       -ast   -> ASTPrinter
       -koopa -> 自定义 IR 文本输出
       -riscv -> RiscvBackend
```

模块职责：

- `front_end`: 词法、语法、AST 数据结构。不能 include IR 头文件。
- `semantic`: 符号表、常量求值、语义错误报告。可以被 lowering 使用。
- `ir`: IR 类型、值、指令、基本块、函数、module、验证器、文本 printer。不能 include AST。
- `lower`: AST 到 IR 的转换。它是允许同时看 AST 和 IR 的桥接层。
- `back_end/riscv`: 栈帧、调用约定、数据布局、指令选择、汇编输出。
- `driver`: 参数解析、输入输出文件管理、编译阶段编排、错误诊断。
- `runtime/scripts`: SysY runtime、baremetal 链接、QEMU 运行和临时文件策略。

## 长期任务拆分

### A0 建立架构基线

目标：把当前行为、支持范围和模块边界记录清楚。

小任务：

- A0.1 维护长期模块流说明，当前在本文档的“模块交互”章节记录 architecture 视角，面试和项目讲解视角见 `docs/project/`。
- A0.2 维护一张“重构支持边界表”，见 `docs/architecture/support_matrix.md`。
  这张表不是为了再次证明编译器能否正常运行，而是为了记录每类 SysY 能力穿过哪些阶段：
  parser/AST 是否能表达，语义/lowering 是否有规则，IR text 是否能打印，RISC-V/baremetal 是否能执行，以及当前依靠哪些测试保护。
  后续拆 `ir_builder.cpp`、`rewind_ir.h`、`riscv.cpp` 时，这张表用来判断某个改动会影响哪些阶段，以及需要跑哪类回归。
- A0.3 固定最小验证用例：`-ast`、`-koopa`、`-riscv`、baremetal run。
- A0.4 记录当前工作区中的临时文件、runtime 构建路径和脚本行为。

验收：

- 文档能解释一份 SysY 程序从输入到输出经过哪些模块。
- 每个模块的当前职责和问题都有记录。
- A0.2 表格能区分“语法已解析但后端无关”、“IR 已表达但后端必须支持”、“runtime 才提供语义”的不同情况。

### A1 头文件解耦

目标：先消除最明显的跨层 include。

状态：第一阶段已完成。

小任务：

- A1.1 从 `include/front_end/ast.h` 移除 `#include "rewind_ir.h"`。
- A1.2 从 `include/ir/symbol_table.h` 移除 `#include "ast.h"`。
- A1.3 检查是否可以用前向声明替代大头文件 include。
- A1.4 为 include 依赖建立简单规则：front_end 不 include ir，ir 不 include front_end。

验收：

- 项目可构建。
- `-ast`、`-koopa`、`-riscv` 行为不变。

### A2 Driver 独立化

目标：让 `main.cpp` 只负责启动 driver。

状态：第一阶段已完成。

小任务：

- A2.1 定义 `CompilerOptions`，替代裸 `argv` 和 `assert(argc == 5)`。
- A2.2 定义 `CompileMode`，统一表示 `-ast`、`-koopa`、`-riscv`。
- A2.3 封装输入文件打开、输出文件打开和错误信息。
- A2.4 引入 `CompilationPipeline`，把 parse、lower、emit 串起来。

验收：

- 错误命令行不会触发 assert，而是输出清晰错误。
- 现有 Makefile 命令不需要改或只做极小改动。

### A3 前端边界整理

目标：让前端成为干净的 AST 生产者。

小任务：

- A3.1 建立 parser wrapper，例如 `parse_sysy(FILE*) -> std::unique_ptr<CompUnitAST>`。
- A3.2 把 AST dump 从节点类中逐步迁到 `ASTPrinter`。
- A3.3 评估是否把 AST 节点放入 `front_end` 或 `ast` namespace。
- A3.4 统一 AST 节点命名和 payload 规则，减少 lowering 中的类型猜测。

验收：

- `-ast` 输出稳定。
- lowering 不直接依赖 parser 生成文件。

### A4 IR Core 拆分

目标：把 `rewind_ir.h` 拆成更稳定的 IR core。

小任务：

- A4.1 拆分 IR 类型、值、指令、基本块、函数、module 定义。
- A4.2 保留 `IRModule` 作为唯一所有者和 factory。
- A4.3 把 target data layout 从 `IRTypeContext` 中移出，形成独立 `DataLayout`。
- A4.4 增加 `IRVerifier`，检查基本块终结、类型匹配、未支持指令等。
- A4.5 明确 IR 支持能力和后端已实现能力的差异。

验收：

- IR core 不 include AST。
- 后端和 IR printer 只依赖 IR core。
- 验证器能在输出前报告结构性错误。

### A5 AST 到 IR Lowering 拆分

目标：拆小 `RewindIRBuilder`。

建议拆分：

- `AstToIrLowerer`: 顶层入口。
- `LoweringContext`: 当前 module、函数、block、作用域、循环目标、命名器。
- `RuntimeDecls`: SysY 库函数声明。
- `ConstEvaluator`: 常量表达式求值。
- `DeclLowerer`: 全局和局部声明。
- `StmtLowerer`: 语句和控制流。
- `ExprLowerer`: 表达式和函数调用。
- `LValueLowerer`: 左值、数组、指针 decay。
- `ArrayInitLowerer`: 常量和运行时数组初始化。
- `SemanticAnalyzer`: 源语言层面的声明收集、符号绑定、类型推断和语义错误检查。
- `SemanticInfo`: lowering 可消费的语义信息，例如表达式类型、左值绑定、函数签名、数组维度和 array-to-pointer decay 决策。

小任务：

- A5.1 先抽 `RuntimeDecls`，因为它最独立。状态：第一阶段已完成，当前落在 `src/ir/ir_builder_runtime_decls.cpp`。
- A5.2 抽 `ConstEvaluator`，让常量求值从 `RewindIRBuilder` 中分离。状态：第一阶段已完成，当前落在 `src/ir/ir_builder_const_eval.cpp`。
- A5.3 抽数组初始化 helper。状态：第一阶段已完成，当前落在 `src/ir/ir_builder_array_init.cpp`。
- A5.4 抽表达式 lowering。状态：第一阶段已完成，当前落在 `src/ir/ir_builder_expr.cpp`。
- A5.5 抽语句和控制流 lowering。状态：第一阶段已完成，当前落在 `src/ir/ir_builder_stmt.cpp`。
- A5.6 语义检查和 lowering 解耦。状态：第一阶段已完成。当前新增 `semantic_checks` helper 和轻量 `VariableSemanticInfo`，先把函数调用、左值可变性、数组下标数量、return、break/continue 等高频语义判断从 lowering 主流程中抽出。后续再逐步扩展更完整的 `SemanticInfo` 和可独立运行的 `SemanticAnalyzer`。

A5.6 建议推进顺序：

1. 先梳理当前 lowering 中的语义检查点，区分 parser 语法错误、SysY 语义错误和 IR 结构错误。
2. 抽出轻量 `SemanticInfo` 数据结构，先记录表达式类型、左值绑定和函数签名，不改变现有 IR 输出。
3. 将函数调用、左值解析、const 赋值、返回类型检查等高频逻辑迁移为独立 semantic helper。
4. 再引入 `SemanticAnalyzer` 作为可独立运行的 AST 遍历阶段，逐步让 lowering 从直接检查转为消费 `SemanticInfo`。
5. 每一步都保留当前 `RewindIRBuilder` 兼容入口，确保 `-koopa`、`-riscv` 和 smoke 测试稳定。

验收：

- 每一步拆分后生成的 IR 文本保持一致。
- 发生语义错误时能定位到负责模块。
- lowering 中的错误判断逐步减少，源语言错误和 IR verifier 错误能清晰分层。

### A7 RISC-V 后端拆分

目标：把 `riscv.cpp` 拆成几个稳定部件。

建议拆分：

- `FrameLayout`: 栈帧大小、对象槽、值槽、参数槽。
- `CallingConvention`: 参数寄存器、栈上传参、返回值规则。
- `AsmWriter`: 低层汇编文本输出。
- `InstructionEmitter`: 单条 IR 指令到汇编。
- `RiscvBackend`: module/function/basic block 遍历和总体调度。
- `DataLayout`: 类型大小和对齐。

小任务：

- A7.1 先抽 `AsmWriter`，因为它基本不改变行为。状态：已完成，当前落在 `include/back_end/asm_writer.h` 和 `src/back_end/asm_writer.cpp`。
- A7.2 再抽 `FrameLayout`。状态：第一阶段已完成，当前落在 `include/back_end/frame_layout.h` 和 `src/back_end/frame_layout.cpp`。
- A7.3 抽 `CallingConvention`。状态：第一阶段已完成，当前落在 `include/back_end/calling_convention.h` 和 `src/back_end/calling_convention.cpp`。
- A7.4 抽 target `DataLayout`，替代直接使用 `IRTypeContext` 的大小和对齐。状态：第一阶段已完成，当前落在 `include/back_end/data_layout.h` 和 `src/back_end/data_layout.cpp`。
- A7.5 给每类 IR 指令建立后端支持表。状态：第一阶段已完成，见 `docs/architecture/riscv_backend_design.md`。

验收：

- 大栈帧、函数调用、数组访问、全局变量相关用例仍通过。
- 后端文件不再单文件承载所有职责。

### A8 Runtime 和脚本整理

目标：让 runtime 构建、临时文件和 baremetal 运行规则清楚稳定。

状态：第一阶段已完成。当前目录边界见 `docs/architecture/test_artifact_layout.md`。

小任务：

- A8.1 保持 runtime 源码在 `third_party/sysyrt/baremetal`。
- A8.2 保持生成物在 `build/sysyrt/riscv32-baremetal`。
- A8.3 移除或忽略无用的 `third_party/sysyrt/riscv32-baremetal` 预编译目录。
- A8.4 统一 `tmp/` 临时目录和清理规则。状态：第一阶段已完成，`tmp/` 只承载 smoke 输出和手工 scratch，单独使用 `make clean-tmp` 清理。
- A8.5 QEMU 由用户手动运行或手动关闭，Makefile 不内置 timeout 逻辑。
- A8.6 保留最终 RISC-V ELF，避免只在临时目录中链接后立即运行。
- A8.7 将 baremetal 构建、链接和运行入口迁移到 `Makefile`。

验收：

- 正常运行不留下 `tmp/sysy-baremetal.*`。
- 正常运行会保留 `riscv32-baremetal/<input-name>/<input-name>.elf`。
- `make clean` 能清理生成物。

### A9 测试和回归保护

目标：每次重构都有低成本验证。

状态：第一阶段已完成，并补充测试资产目录边界。

小任务：

- A9.1 建立小型 golden tests：AST、IR text、RISC-V asm。状态：第一阶段已完成，当前提供 `make regression-smoke` 的本地 smoke 输出入口，基础输入位于 `tests/smoke/`。
- A9.2 建立 baremetal smoke tests。状态：已有 `make run-riscv-baremetal`，本轮未改变。
- A9.3 为曾经修过的问题加入用例：`!=`、`!!x`、`- -1`、大栈帧、数组初始化、函数调用。状态：第一阶段先保留在支持矩阵和 smoke 脚本中，后续可继续升级为 golden 文件对比。
- A9.4 区分 baseline breakage 和当前任务引入的问题。状态：第一阶段通过 `scripts/run_regression_smoke.sh` 固定最小本地验证链路；Docker autotest 通过 `RUN_DOCKER_AUTOTEST=1` 显式打开。
- A9.5 固定测试资产和临时产物目录边界。状态：第一阶段已完成，`tests/` 保存可追踪输入/fixture，`tmp/` 保存可再生输出。

验收：

- 每个架构任务都能说明运行了哪些验证。
- 回归失败时能定位到前端、lowering、IR、后端或 runtime。

### A10 IR Verifier 和 Pass Manager

目标：在继续添加优化之前，先让 IR 有结构校验和统一 pass 管线。

状态：第一阶段已完成。当前实现只建立 IR 输出前安全网和 pass 管线入口，不引入实际优化，也不替代源语言语义检查。

说明：A10 到 A13 是主线重构完成后的下一阶段能力建设。A11 已拆到 `docs/architecture/ssa_mem2reg_design.md`；
A10、A12、A13 仍先保留在长期任务文档中，等各自边界收敛后再分别建立独立任务文档。

A10 不替代 A5.6 的语义检查。A5.6 面向 SysY 源语言规则，A10 面向已经生成的 IR 内部不变量，例如控制流完整、跳转目标有效、操作数类型匹配、函数调用和返回值类型一致。

小任务：

- A10.1 建立 `IRVerifier`，检查函数签名、基本块终结指令、跳转目标、指令操作数类型、`load/store/call/return` 类型匹配。状态：第一阶段已完成。
- A10.2 明确 verifier 在 pipeline 中的位置，至少在 `-koopa` 和 `-riscv` 输出前可以运行。状态：第一阶段已完成，当前 driver 在 pass 前后各验证一次。
- A10.3 设计轻量 `Pass` / `PassManager` 接口，先支持 module/function 级别 pass。状态：第一阶段已完成。
- A10.4 接入一个 no-op pass 或统计 pass，验证 pass 管线不改变 IR 行为。状态：第一阶段已完成，当前接入 `IRNoOpModulePass`。
- A10.5 建立 verifier/pass 相关最小测试，覆盖错误 IR、合法 IR 和 no-op pass 输出一致性。状态：第一阶段已完成，当前提供 `make ir-verifier-smoke`。

第一阶段实现边界：

- `IRVerifier` 只检查 IR 内部不变量，不负责 SysY 语义规则，例如是否允许修改 const、是否处在 while 内使用 break。
- `IRPassManager` 当前只提供 module/function pass 的顺序执行能力，no-op pass 用来验证管线接入，不改变 IR。
- 后续进入 A11/A12 时，每个新增 IR 表达能力或优化 pass 都应同步补 verifier 规则和 smoke/golden 用例。

验收：

- 结构错误能在输出前报告，而不是等到 IR text assembler 或 RISC-V 后端崩溃。
- pass manager 能按顺序运行多个 pass，并保持无优化时的输出稳定。
- 后续优化 pass 不再直接塞进 driver 或 IR builder。

### A11 SSA 和 mem2reg

目标：把当前以 `alloc/load/store` 为主的局部标量表示，逐步提升为 SSA value，形成真正适合优化的 IR。

状态：A11.1-A11.5 第一阶段已完成。当前 IR 已支持 basic block parameters、branch/jump edge args、`IRBlockArgRef::owner_` 和 `IRModule::make_block_param` 统一创建入口；`CFGAnalysis` 已能从 terminator 计算 successor/predecessor 与 reachable blocks；`DominanceAnalysis` 已能计算 immediate dominator、dominator tree children 和 dominance frontier；`ir_rewrite` 已提供 operand 遍历/替换和 block instruction erase 基础接口；`Mem2RegPass` 已能作为可选 `IRModulePass` 提升局部标量 `alloc i32`，并在 hand-written IR smoke 中生成 block arguments。普通 SysY lowering 和 driver 默认输出仍保持 memory-form IR，后续 A11.6 处理 SSA IR 到 Machine IR 的过渡。

专项设计：见 `docs/architecture/ssa_mem2reg_design.md`。

方案摘要：

- SSA 合流表示采用 basic block arguments 与控制流边参数，不在第一版引入单独的 `IRPhiInst`。
- 先补 block argument IR、printer 和 verifier，再建立 CFG、dominance 与 IR rewrite 基础设施。
- `mem2reg` 第一版只提升局部、非逃逸、标量 `alloc i32`，数组和地址语义继续走当前内存路径。
- A11.6 倾向先做 SSA Rewind IR 到 Machine IR 的过渡层，用 Machine IR/Verifier 验证 block argument lowering；完整 Machine IR 到 RISC-V asm、寄存器分配和 QEMU 闭环放到后续 A13。

验收：

- mem2reg 前后程序语义一致。
- block params 与 edge args 经过 verifier 检查。
- 数组、指针、函数参数等复杂地址语义不会被错误提升。

### A12 基础 IR 优化 Pass

目标：在 verifier、pass manager 和 SSA 能力稳定后，补齐最基础、最能体现 IR 价值的优化。

小任务：

- A12.1 常量折叠和常量传播，复用但不污染现有编译期常量求值逻辑。
- A12.2 死代码删除，删除无副作用且结果未使用的指令。
- A12.3 不可达基本块删除。
- A12.4 CFG 简化，合并空跳转块、简化恒定条件分支。
- A12.5 简单公共子表达式消除或局部值编号，作为后续更强优化的起点。
- A12.6 为每个 pass 明确依赖条件，例如是否要求 SSA、是否会改变 CFG、是否需要重新运行 verifier。

验收：

- 每个 pass 可以单独打开和验证。
- pass 顺序有文档说明。
- 优化前后 `-koopa` / `-riscv` / baremetal 语义一致。

### A13 RISC-V 寄存器分配优化

目标：在当前栈式后端稳定的基础上，减少不必要的 load/store，让后端从“能正确生成代码”进一步走向“生成质量可解释”。

小任务：

- A13.1 先整理后端内部值表示，明确哪些 IR value 可以映射到虚拟寄存器，哪些必须留在栈上。
- A13.2 实现 live interval 或基础 liveness analysis。
- A13.3 先做 linear scan register allocation，优先覆盖普通标量表达式和基本块内临时值。
- A13.4 处理调用约定中的 caller-saved/callee-saved、参数寄存器、返回值寄存器和 spill slot。
- A13.5 保留栈式 fallback，遇到数组、大对象、复杂地址或未支持情况时仍能正确生成代码。
- A13.6 用简单算术、函数调用、多参数、大栈帧和数组程序分别验证正确性。

验收：

- 现有测试仍通过。
- 典型标量程序的汇编中无意义栈读写明显减少。
- 寄存器分配失败时能安全 fallback，而不是生成错误汇编。
