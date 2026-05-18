# RISC-V Backend Support Table

这份表对应 A7.5，用来记录当前 RISC-V 后端对自定义 IR 的支持边界。

它不是 SysY 语法支持表，而是后端维护表：当新增 IR 指令、修改 lowering、拆分 `riscv.cpp` 或引入 Machine IR 时，先看这里判断后端是否需要同步更新。

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

## 当前有意保留的限制

| Limitation | Reason | Future direction |
| --- | --- | --- |
| 没有 Machine IR | 当前优先保证架构拆分和行为稳定 | A10-A13 之后再考虑 `MachineFunction/MachineInstr/VirtualReg` |
| 没有全局寄存器分配 | 当前所有长期值都以栈槽为真值来源 | A13 先做 liveness + linear scan，并保留栈式 fallback |
| 没有独立 instruction scheduling | 当前按 IR 顺序线性输出，方便调试 | 引入 Machine IR 后再做 peephole 和局部调度 |
| RV32 数据布局写死 | 当前 baremetal 工具链使用 `rv32im/ilp32` | 后续由 `DataLayout` 扩展为 target/subtarget 参数 |
| 后端错误以 exception 抛出 | 当前 driver 仍未统一 diagnostics | 后续和 driver diagnostics / IR verifier 对接 |

## 修改后端时的最小验证

```bash
cmake --build build -j12 -- -s
build/compiler -koopa tests/hello.sysy -o /tmp/sysy_hello.koopa
build/compiler -riscv tests/hello.sysy -o /tmp/sysy_hello.s
build/compiler -riscv tests/awesome-sysy/lisp.c -o /tmp/sysy_lisp.s
```

如果改到调用约定、栈帧或数组寻址，还应继续运行：

```bash
docker run -i --rm -v /Users/qingxuliang/project/compiler/SysY:/root/compiler maxxing/compiler-dev autotest -riscv -s lv9 /root/compiler
```
