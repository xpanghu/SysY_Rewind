# RISC-V Backend Design

这份文档记录 RISC-V 后端的长期设计、当前支持边界，以及后续引入最小 Machine IR 的演进路线。

它不是 SysY 语法支持表，而是后端维护文档：当新增 IR 指令、修改 lowering、拆分 `riscv.cpp` 或引入 Machine IR 时，先看这里判断后端是否需要同步更新。

## 后端分层现状

当前后端仍然是 correctness-first 的栈式代码生成器，但已经开始拆出稳定边界：

| Component | File | Responsibility |
| --- | --- | --- |
| `AsmWriter` | `include/back_end/asm_writer.h`, `src/back_end/asm_writer.cpp` | 输出 RISC-V/GAS 风格汇编文本，封装寄存器名、label、指令和 data/text directive |
| `DataLayout` | `include/back_end/data_layout.h`, `src/back_end/data_layout.cpp` | 描述当前 RV32 目标的数据大小、栈对齐和 IR value 在栈上的存储大小 |
| `CallingConvention` | `include/back_end/calling_convention.h`, `src/back_end/calling_convention.cpp` | 描述参数寄存器 `a0-a7`、返回寄存器和栈上传参位置 |
| `FrameLayout` | `include/back_end/frame_layout.h`, `src/back_end/frame_layout.cpp` | 预扫描函数并分配 outgoing arg 区、局部对象槽、临时值槽和 `ra` 保存槽 |
| `InstructionSelector` | `include/back_end/instruction_selector.h`, `src/back_end/instruction_selector.cpp` | 遍历 Rewind IR function/basic block，并把单条 IR 指令 lowering 到 stack-backed Machine IR |
| `Machine IR` | `include/back_end/machine_ir.h`, `src/back_end/machine_ir.cpp` | 表示 MachineFunction、MachineBasicBlock、MachineInstr、MachineOperand 和 MachineFrame |
| `MachineVerifier` | `include/back_end/machine_verifier.h`, `src/back_end/machine_verifier.cpp` | 检查 Machine IR block 是否有 terminator 等第一阶段结构约束 |
| `RISC-V backend entry` | `include/back_end/riscv.h`, `src/back_end/riscv.cpp` | 对 driver 暴露 `emit_module`，内部只调用 `MachineAsmPrinter`，不再保留历史栈式 `IREmitter` |

## IR 指令支持表

| IR kind | RISC-V support | Current lowering strategy | Notes |
| --- | --- | --- | --- |
| `IR_CONSTANT` | supported as operand | `li rd, imm` | 常量本身不是指令，不单独 emit |
| `IR_ZERO_INIT` | supported in initializer | `.zero size` for global, recursive `sw x0` for local aggregate | 依赖 `DataLayout::type_size` |
| `IR_AGGREGATE` | supported in initializer | global emits nested `.word`; local emits recursive stores | 主要服务数组初始化 |
| `IR_GLOBALALLOC` | supported | `.data` + global label + initializer | 作为 rvalue 时会 load，作为 pointer 时会 `la` |
| `IR_ALLOC` | supported as stack object | no emitted instruction; `FrameLayout` reserves object slot | 标量和数组都走 object slot |
| `IR_STORE` | supported | scalar: materialize + store; aggregate: recursive initializer stores | destination must be addressable |
| `IR_LOAD` | supported | materialize pointer, then `lw`, then spill result slot | 当前只覆盖 i32/pointer word load |
| `IR_BINARY` | supported | direct opcode mapping or short instruction sequence | `eq/ne/le/ge` 会展开为 `xor/slt + seqz/snez` |
| `IR_CALL` | supported | first 8 args in `a0-a7`, rest in outgoing arg stack area; result in `a0` | `FrameLayout` reserves `ra` slot if any call exists |
| `IR_RETURN` | supported | optional return value in `a0`, emit epilogue and `ret` | void return does not materialize value |
| `IR_JUMP` | supported | `j .Lfunction_block` | block label includes function prefix to avoid collision |
| `IR_BRANCH` | supported | materialize cond, `bnez true`, `j false` | 当前不做 fallthrough 优化 |
| `IR_GET_PTR` | supported | base pointer + index * pointee_size | 用于 pointer arithmetic / array parameter |
| `IR_GET_ELEM_PTR` | supported | aggregate storage address + index * element_size | source must be pointer-to-array storage |
| `IR_FUNC_ARG_REF` | supported as operand | `a0-a7` direct move or caller-frame stack load | 不单独 emit |
| function declaration | ignored by backend | declaration-only function is skipped | call target still emitted as external symbol |

## A13 Machine IR 演进方案

当前后端链路已经切到第一阶段 Machine IR MVP：

```text
Rewind IR
  -> InstructionSelector
  -> MachineFunction / MachineBasicBlock / MachineInstr
  -> MachineVerifier
  -> MachineAsmPrinter
  -> AsmWriter
  -> RISC-V asm
```

`riscv.cpp` 现在只是一个很薄的入口文件；默认 `-riscv` 输出只通过 Machine IR 路径生成。

A13 统一承接后端 Machine IR 演进。长期目标不是复刻 LLVM/GCC 的完整 Machine SSA，而是先引入一条可运行、可验证、可继续扩展的后端链路：

```text
Rewind IR
  -> InstructionSelector
  -> MachineFunction / MachineBasicBlock / MachineInstr
  -> stack-backed FrameLayout + Prologue/Epilogue
  -> MachineAsmPrinter
  -> AsmWriter
  -> RISC-V asm
```

### A13 第一阶段目标

第一阶段是 **Machine IR MVP: stack-backed codegen path**。它不追求性能，也不实现真正寄存器分配，而是把当前栈式后端拆进 Machine IR 架构中：

- `InstructionSelector` 完成 Rewind IR 到 Machine IR 的平铺和映射。
- Machine IR 使用 virtual register / frame index / machine block / machine opcode 表达机器级结构。
- 在没有 RA 前，为需要落地的 virtual register 分配 stack home slot。
- `Prologue/Epilogue` 负责最终栈帧开辟、`ra` 保存恢复和返回路径收尾。
- `MachineAsmPrinter` 把 Machine IR 转成 `AsmWriter` 调用，输出 RISC-V/GAS 汇编文本。
- 历史 `IREmitter` 已删除，`riscv.cpp` 只保留后端入口函数。

第一阶段明确暂缓：

- SelectionDAG / GlobalISel 级别复杂 pattern matching。
- Peephole optimization。
- Block placement。
- Liveness analysis。
- Linear scan / graph coloring register allocation。
- Spill cost 决策。
- Instruction scheduling。

第一阶段已新增代码结构：

```text
include/back_end/
  machine_ir.h              # MachineFunction / MachineBasicBlock / MachineInstr / MachineOperand
  instruction_selector.h    # Rewind IR -> Machine IR
  machine_asm_printer.h     # Machine IR -> AsmWriter
  machine_verifier.h        # 检查 Machine IR 合法性
  machine_frame.h           # Machine frame slots / stack home / saved registers

src/back_end/
  machine_ir.cpp
  instruction_selector.cpp
  machine_asm_printer.cpp
  machine_verifier.cpp
```

可以先不创建 `MachineModule`。全局变量目前直接由后端输出 `.data` 已经够用，第一阶段只把函数体 lowering 成 `MachineFunction`。

### A13 和现有后端设施的关系

| Facility | A13 第一阶段策略 | 原因 |
| --- | --- | --- |
| `DataLayout` | 复用 | 用于确认 word size、指针大小、栈对齐和 frame slot 大小 |
| `CallingConvention` | 复用并逐步迁移 | 函数参数、返回值、caller outgoing args 仍需要遵守 RISC-V ABI |
| `FrameLayout` | 复用概念，必要时拆出 `MachineFrame` | 当前 `FrameLayout` 面向 Rewind IR value 栈槽；Machine IR 需要同时管理 stack home、spill/home slot、saved registers 和 outgoing args |
| `AsmWriter` | 继续作为最终文本输出层 | `MachineAsmPrinter` 不直接拼字符串，而是调用 `AsmWriter` 输出 GAS 风格汇编 |
| `riscv.cpp` | 只保留薄入口 | driver 继续调用 `riscv::emit_module`，实际后端实现集中在 Machine IR 相关组件中 |

这个边界很重要：A13 第一阶段不是一次性做完整优化后端，而是先把 **Rewind IR -> Machine IR -> stack-backed RISC-V asm** 路径打通。

### Block Arguments 的 Machine IR 表示

Rewind IR 的 block arguments：

```text
%then:
  jump %merge(%x_then)

%else:
  jump %merge(%x_else)

%merge(%x: i32):
  ret %x
```

下降到 Machine IR 时，不应把 incoming value 混成普通顺序 copy，而应先保留“边上的并行拷贝”
语义：

```text
mbb .Lthen:
  jump .Lmerge { v_merge_x <- v_x_then }

mbb .Lelse:
  jump .Lmerge { v_merge_x <- v_x_else }

mbb .Lmerge(v_merge_x: i32):
  ret v_merge_x
```

因此第一版 `MachineInstr` 需要支持 `PARALLEL_COPY` 或 terminator edge-copy payload。推荐先用
terminator edge-copy payload 表示，因为它和 Rewind IR 的 edge args 一一对应，`MachineVerifier`
也更容易检查：

- 目标 machine block 的 params 数量。
- 每条 predecessor edge 的 incoming value 数量。
- incoming value 类型和目标 block param 类型是否一致。
- true edge 和 false edge 的 incoming value 不会互相串线。

在 stack-backed MVP 中，edge-copy 可以先 lowering 成进入目标 block 前的具体 copy/store 序列；
后续引入寄存器分配后，再把这些 parallel copy 交给 copy resolver 处理。

最小数据结构可以是：

```cpp
enum class MachineOperandKind {
    PhysReg,
    VirtReg,
    Imm,
    FrameIndex,
    GlobalSymbol,
    BasicBlock,
};

enum class MachineOpcode {
    LI,
    COPY,
    PARALLEL_COPY,
    LA,
    LW,
    SW,
    ADD,
    ADDI,
    SUB,
    MUL,
    DIV,
    REM,
    XOR,
    SLT,
    SEQZ,
    SNEZ,
    CALL,
    RET,
    J,
    BNEZ,
    PROLOGUE,
    EPILOGUE,
};

struct MachineOperand {
    MachineOperandKind kind;
    // register id / immediate / frame index / symbol / block label
};

struct MachineInstr {
    MachineOpcode opcode;
    std::vector<MachineOperand> operands;
};

struct MachineBasicBlock {
    std::string label;
    std::vector<MachineOperand> params;
    std::vector<MachineInstr> instrs;
};

struct MachineFunction {
    std::string name;
    MachineFrame frame;
    std::vector<MachineBasicBlock> blocks;
};
```

Machine IR 第一阶段可以同时支持 virtual register 和少量 scratch physical registers：

例如当前 `add` 可能直接生成：

```asm
lw t0, 0(sp)
lw t1, 4(sp)
add t0, t0, t1
sw t0, 8(sp)
```

stack-backed Machine IR MVP 可以先表示为：

```text
%v1 = LW frame[0]
%v2 = LW frame[4]
%v3 = ADD %v1, %v2
SW   %v3, frame[8]
```

在没有 RA 前，`MachineAsmPrinter` 或其前置 lowering 可以把 `%v1/%v2/%v3` 通过 stack home slot
和 `t0/t1/t2` 临时寄存器 materialize 成真实汇编。等路径稳定后，再进入真正寄存器分配：

```text
%v1, %v2, %v3 -> t0/t1/a0 或 spill slot
```

### A13 分阶段推进

第一阶段：Machine IR MVP。

1. 定义 `MachineInstr`、`MachineOperand`、`MachineFunction`、`MachineFrame`。
2. 实现 `InstructionSelector` 的直接映射，覆盖当前 `-riscv` smoke 所需 IR 指令。
3. 实现 block argument edge-copy / parallel-copy 表示。
4. 实现 Machine verifier / printer。
5. 实现 stack-backed frame layout 和 prologue/epilogue insertion。
6. 实现 `MachineAsmPrinter -> AsmWriter -> RISC-V asm`。
7. 删除历史栈式后端实现，避免长期维护两套 RISC-V lowering。

第二阶段：寄存器分配。

1. 建立 Machine liveness。
2. 实现 live interval。
3. 先做 linear scan register allocation。
4. 处理 caller-saved / callee-saved、spill slot 和 copy resolution。
5. 保留 stack-backed fallback，避免寄存器分配失败时生成错误汇编。

第三阶段：机器级优化。

1. Peephole optimization。
2. Block placement。
3. Instruction scheduling。

第一阶段的成功标准不是性能提升，而是后端边界变清晰：指令选择、机器级优化、寄存器分配、栈帧布局和汇编打印可以各自独立演进。

## 当前有意保留的限制

| Limitation | Reason | Future direction |
| --- | --- | --- |
| Machine IR 仍是 stack-backed MVP | 第一阶段优先保持 `-riscv` 正确性和后端边界清晰 | A13 后续阶段逐步让 virtual register 成为主要值载体 |
| 没有全局寄存器分配 | 当前所有长期值仍以栈槽为真值来源 | A13 后续阶段再做 liveness + linear scan，并保留 stack-backed Machine IR 兜底路径 |
| 没有独立 instruction scheduling | 当前按 IR 顺序线性输出，方便调试 | A13 后续阶段在 Machine IR 上补 peephole、block placement 和局部调度 |
| RV32 数据布局写死 | 当前 baremetal 工具链使用 `rv32im/ilp32` | 后续由 `DataLayout` 扩展为 target/subtarget 参数 |
| 后端错误以 exception 抛出 | 当前 driver 仍未统一 diagnostics | 后续和 driver diagnostics / IR verifier 对接 |

## 修改后端时的最小验证

```bash
cmake --build build -j12 -- -s
build/compiler -koopa tests/smoke/hello.sysy -o /tmp/sysy_hello.koopa
build/compiler -riscv tests/smoke/hello.sysy -o /tmp/sysy_hello.s
build/compiler -riscv tests/awesome-sysy/lisp.c -o /tmp/sysy_lisp.s
```

如果改到调用约定、栈帧或数组寻址，还应继续运行：

```bash
docker run -i --rm -v /Users/qingxuliang/project/compiler/SysY:/root/compiler maxxing/compiler-dev autotest -riscv -s lv9 /root/compiler
```
