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
| `IREmitter` | `include/back_end/riscv.h`, `src/back_end/riscv.cpp` | 遍历 IR module/function/basic block，并把单条 IR 指令 lowering 到 RISC-V 指令序列 |

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

## 最小 Machine IR 演进方案

当前后端链路是：

```text
Rewind IR
  -> IREmitter 直接边选择指令边调用 AsmWriter
  -> RISC-V asm
```

这条链路短、容易验证，但 `IREmitter` 同时承担 IR 遍历、指令选择、临时寄存器使用、栈帧访问和汇编打印调度。后续如果要实现寄存器分配、peephole、局部调度或更系统的调用约定处理，需要在 Rewind IR 和汇编文本之间增加一层轻量机器级表示。

第一阶段目标不是复刻 LLVM/GCC 的完整 Machine SSA，而是引入最小 Machine IR：

```text
Rewind IR
  -> InstructionSelector
  -> MachineFunction / MachineBasicBlock / MachineInstr
  -> MachineAsmPrinter
  -> AsmWriter
  -> RISC-V asm
```

建议新增代码结构：

```text
include/back_end/
  machine_ir.h              # MachineFunction / MachineBasicBlock / MachineInstr / MachineOperand
  machine_opcode.h          # RISC-V 机器指令枚举
  instruction_selector.h    # Rewind IR -> Machine IR
  machine_asm_printer.h     # Machine IR -> AsmWriter
  machine_verifier.h        # 可选，检查 Machine IR 合法性

src/back_end/
  machine_ir.cpp
  instruction_selector.cpp
  machine_asm_printer.cpp
  machine_verifier.cpp
```

可以先不创建 `MachineModule`。全局变量目前直接由后端输出 `.data` 已经够用，第一阶段只把函数体 lowering 成 `MachineFunction`。

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
    std::vector<MachineInstr> instrs;
};

struct MachineFunction {
    std::string name;
    FrameLayout frame;
    std::vector<MachineBasicBlock> blocks;
};
```

第一阶段仍然可以使用物理寄存器 `t0/t1/t2/a0-a7`，不立即做虚拟寄存器分配。这样改造的收益是：先把“直接打印汇编”变成“先生成机器指令对象，再统一打印”，但生成汇编基本保持不变。

例如当前 `add` 可能直接生成：

```asm
lw t0, 0(sp)
lw t1, 4(sp)
add t0, t0, t1
sw t0, 8(sp)
```

最小 Machine IR 第一阶段可以表示为：

```text
LW   t0, frame[0]
LW   t1, frame[4]
ADD  t0, t0, t1
SW   t0, frame[8]
```

等 Machine IR 路径稳定后，再引入虚拟寄存器：

```text
v1 = LW frame[0]
v2 = LW frame[4]
v3 = ADD v1, v2
RET v3
```

之后寄存器分配再把 `v1/v2/v3` 映射到真实寄存器或 spill slot。

建议引入顺序：

1. 保留现有 `FrameLayout`、`CallingConvention`、`DataLayout`、`AsmWriter` 不动。
2. 新增 `MachineInstr`、`MachineOperand`、`MachineFunction` 数据结构。
3. 把当前 `IREmitter::emit_binary`、`emit_load`、`emit_store` 这类逻辑逐步改成生成 `MachineInstr`，但仍然使用 `t0/t1/t2` 和栈槽。
4. 新增 `MachineAsmPrinter`，只负责把 `MachineInstr` 翻译成 `AsmWriter` 调用。
5. 稳定后再引入 `VirtReg`，让表达式结果先落到虚拟寄存器，而不是立即 spill 到栈。
6. 最后再做 liveness 和 linear scan register allocation。

这个阶段的成功标准不是性能提升，而是后端边界变清晰：指令选择、机器级优化、寄存器分配、汇编打印可以各自独立演进。

## 当前有意保留的限制

| Limitation | Reason | Future direction |
| --- | --- | --- |
| 没有 Machine IR | 当前优先保证架构拆分和行为稳定 | 先按本文“最小 Machine IR 演进方案”加入 `MachineFunction/MachineInstr/VirtualReg` |
| 没有全局寄存器分配 | 当前所有长期值都以栈槽为真值来源 | A13 先做 liveness + linear scan，并保留栈式 fallback |
| 没有独立 instruction scheduling | 当前按 IR 顺序线性输出，方便调试 | 引入 Machine IR 后再做 peephole 和局部调度 |
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
