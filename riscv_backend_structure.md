# RISC-V Backend 结构说明

## 目标

当前 `riscv` 后端只针对你现在编译器已经支持的这部分 SysY 语法工作：

- 单个 `int` 返回函数
- 局部 `const int` / `int` 声明
- 赋值语句
- `return Exp;`
- 一元、算术、关系、相等、逻辑表达式

实现策略刻意保持简单稳定：

- 所有变量一律放在栈帧上
- 所有中间结果也落到栈槽里
- 代码生成时只使用少量临时寄存器：`t0` / `t1` / `t2`，返回寄存器使用 `a0`

这样做的好处是：

- 不需要复杂寄存器分配
- IR 到汇编的映射非常直接
- 后续扩展语法时容易定位问题

## 文件结构

### `include/back_end/riscv.h`

对外暴露三个核心部分：

1. `Register`
   - 只保留当前实现真正需要的寄存器枚举：`x0`、`ra`、`sp`、`t0`、`t1`、`t2`、`a0`

2. `FunctionFrame`
   - 负责扫描单个 `IRFunction`
   - 为 `IRAllocInst` 分配“对象槽位”
   - 为 `IRLoadInst`、`IRBinaryInst` 分配“值槽位”
   - 计算对齐后的 `frame_size`
   - 记录 `ra` 保存位置

3. `IREmitter`
   - 负责把 `rewind_ir::IRModule` 发射成 RISC-V 汇编
   - 内部包含：
     - 模块/函数/基本块遍历
     - 各类 IR 指令的发射
     - 栈地址、栈读写、立即数和基础指令辅助函数

### `src/back_end/riscv.cpp`

实现分为四层：

1. 栈帧布局
   - `FunctionFrame::build`
   - 先扫描整个函数，统一分配所有栈槽

2. IR 遍历
   - `emit_module`
   - `emit_function`
   - `emit_basic_block`
   - `emit_instruction`

3. 指令级 lowering
   - `emit_store`
   - `emit_load`
   - `emit_binary`
   - `emit_return`

4. 汇编基础设施
   - `emit_prologue` / `emit_epilogue`
   - `emit_stack_address`
   - `emit_stack_load`
   - `emit_stack_store`
   - `emit_li` / `emit_add` / `emit_sub` / `emit_mul` 等

## 栈帧设计

当前函数栈帧布局是：

1. 低地址部分放局部变量对象槽位
2. 接着放 IR 中间值槽位
3. 高地址部分放保存的 `ra`
4. 整体按 16 字节对齐

例如：

```text
sp + 0    : 局部变量/中间值
sp + 4    : 局部变量/中间值
...
sp + N    : 局部变量/中间值
sp + M    : 保存的 ra
```

这里有两个槽位表：

- `object_slots_`
  - key 是 `IRAllocInst*`
  - 表示某个源语言变量真正放在哪个栈偏移

- `value_slots_`
  - key 是会产出值的 IR 指令
  - 当前主要是 `IRLoadInst*`、`IRBinaryInst*`
  - 表示某个表达式结果临时保存在哪个栈偏移

## 生成流程

### 1. 模块级

`emit_module` 会：

- 检查当前 IR 是否只包含本后端支持的子集
- 输出 `.text`
- 输出每个函数的 `.globl`
- 逐个进入函数生成

### 2. 函数级

`emit_function` 会：

1. 调用 `frame_.build(func)` 预先分配整个函数的栈槽
2. 输出函数标签
3. 输出函数前导
4. 顺序发射基本块内的 IR

函数前导为：

```asm
addi sp, sp, -frame_size
sw   ra, ra_offset(sp)
```

函数结尾由 `emit_return` 直接输出：

```asm
lw   ra, ra_offset(sp)
addi sp, sp, frame_size
ret
```

### 3. 表达式值的处理

`materialize_value` 的规则：

- 常量：直接 `li`
- 有值槽位的 IR：从对应栈槽 `lw` 出来
- `alloc` 本身作为值使用：把它看成地址，生成 `sp + offset`

`spill_value` 的规则：

- 对 `IRLoadInst` / `IRBinaryInst` 的结果统一写回自己的值槽位

于是整个表达式 lowering 是：

1. 从栈上或立即数把操作数放到 `t0` / `t1`
2. 在寄存器里完成一次运算
3. 把结果写回自己的栈槽

## 指令映射

当前后端实际支持的 IR 指令只有：

- `IRAllocInst`
- `IRStoreInst`
- `IRLoadInst`
- `IRBinaryInst`
- `IRReturnInst`
- `IRConstant`

对应规则如下。

### `IRAllocInst`

- 只参与栈槽分配
- 不单独生成汇编

### `IRStoreInst`

- 先把 `value_` 取到 `t0`
- 如果 `dest_` 是局部变量 `alloc`，直接存到变量槽位
- 否则把 `dest_` 当成指针值，先取地址再 `sw`

### `IRLoadInst`

- 如果 `src_` 是局部变量 `alloc`，直接从变量槽位 `lw`
- 否则把 `src_` 当成指针值，先取地址再 `lw`
- 结果写回自己的值槽位

### `IRBinaryInst`

统一约定：

- 左操作数进 `t0`
- 右操作数进 `t1`
- 结果仍写回 `t0`
- 最后把 `t0` spill 到当前 IR 的值槽位

主要映射关系：

- `ADD` -> `add`
- `SUB` -> `sub`
- `MUL` -> `mul`
- `DIV` -> `div`
- `MOD` -> `rem`
- `LT` -> `slt`
- `GT` -> `slt t0, t1, t0`
- `LE` -> `slt` + `seqz`
- `GE` -> `slt` + `seqz`
- `EQ` -> `xor` + `seqz`
- `NEQ` -> `xor` + `snez`
- `AND` / `OR` / `XOR` -> 位运算

## 大栈帧处理

RISC-V 的 `lw/sw/addi` 立即数只有 12 位。

所以当前实现专门加了：

- `emit_stack_address`
- `emit_stack_load`
- `emit_stack_store`

当偏移落在 `[-2048, 2047]` 内时，直接生成：

```asm
lw t0, offset(sp)
sw t0, offset(sp)
```

当偏移超范围时，自动退化为：

```asm
li  t2, offset
add t2, sp, t2
lw  t0, 0(t2)
```

或：

```asm
li  t2, offset
add t2, sp, t2
sw  t0, 0(t2)
```

这保证了即使表达式很长、临时值很多，后端仍然能正确生成汇编。

## 配套修复

为了让当前语法子集真正可用，还顺手修了 `src/ir/rewind_ir_builder.cpp` 里两处和本后端直接相关的问题：

1. `EqExp` 以前无论 `==` 还是 `!=` 都错误地生成 `IRBinaryOp::EQ`
   - 现在改为根据 AST 运算符选择 `EQ` 或 `NEQ`

2. 常量求值里的嵌套一元表达式递归写错了
   - 以前会把 `UnaryExpAST::Unary` 直接当成 `PrimaryExpAST`
   - 现在改成递归处理 `UnaryExpAST`

## 后续扩展建议

如果之后要继续扩展语法，推荐按下面顺序推进：

1. 先在 `rewind_ir_builder.cpp` 中补齐对应 IR
2. 再在 `FunctionFrame` 里决定哪些新 IR 需要独立值槽位
3. 最后在 `emit_instruction` 中增加对应 lowering

如果未来要支持：

- 分支
- 多基本块
- 函数调用
- 数组/指针

建议继续保持“先全函数扫描，再统一布局栈帧”的思路，这样会比先做寄存器分配更容易保持正确性。
