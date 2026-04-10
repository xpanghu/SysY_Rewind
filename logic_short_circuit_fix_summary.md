# 短路求值与 `if/else` 修复总结

## 本轮主要问题

### 1. 变量初始化中的逻辑表达式会把 `store` 落到错误的 basic block

问题现象：

- `int a = 1 || 2 && 3;`
- `int b = 0 && 1 || 0;`
- `int c = (1 && 0 || 1) * 4;`

在这些初始化里，`lower_exp(...)` 可能因为短路求值而构建新的 CFG，并把 `FuncContext.current_block` 推进到 merge block。

但 `lower_var_decl(...)` 之前先缓存了旧的 `current_block`，导致：

- `store exp_value, @var`
- 被错误地追加到分支前的旧块

修复方式：

- 在 `lower_var_decl(...)` 中，`lower_exp(...)` 返回后重新通过 `require_current_block(ctx)` 获取当前插入块
- 再把 `store` 插入到正确的 merge block 中

对应文件：

- `src/ir/ir_builder.cpp`

### 2. `lower_stmt(...)` 在 `return`、赋值、`if` 条件里提前缓存旧 `current_block`

问题本质：

- 引入 `if/else` 和短路求值后，`lower_exp(...)` 不再只是“生成一个值”
- 它还可能推进当前 CFG，并改变 `FuncContext.current_block`

如果先缓存旧块，再去 `lower_exp(...)`，就会出现：

- `ret`
- `store`
- `br`

被追加到已经发出 terminator 的旧块中。

修复方式：

- 统一改成：
  1. 先 `lower_exp(...)`
  2. 再重新读取 `require_current_block(ctx)`
  3. 最后追加 `ret/store/br`

对应文件：

- `src/ir/ir_builder.cpp`

### 3. `||` / `&&` 的 RHS 布尔化结果没有分配名字

问题现象：

- koopa 文本里出现空名字
- 例如 `= ne 2, 0`
- 进而导致 `store , @tmp`

根因：

- `rhs_bool` 的 `IRBinaryInst` 构造时没有传 `ctx.next_value_name()`

修复方式：

- 在 `lower_lor_exp(...)` 和 `lower_land_exp(...)` 的 RHS 布尔化处补上名字分配

对应文件：

- `src/ir/ir_builder.cpp`

### 4. `&&` 的 CFG 方向和 RHS lowering 目标需要严格区分

正确语义：

- `lhs != 0` 时才求值 RHS
- `lhs == 0` 时直接短路为 `0`

因此：

- `br lhs_bool, rhs_bb, short_false`

而不是反过来。

同时 RHS 应该 lower 当前节点的右孩子：

- `lower_eq_exp(eq_exp, ctx)`

而不是错误递归回左侧链。

对应文件：

- `src/ir/ir_builder.cpp`

### 5. `if-else` 两边都终结时不应生成空 merge block

问题现象：

- koopa 文本尾部出现空 `%end_x:`
- 没有任何指令
- Koopa 解析时会报块未正常终结

修复方式：

- 对 `if-else`：
  - 只有当 then/else 至少一边还能继续执行时，才创建 merge block
- 对 `if` 无 `else`：
  - 保留 merge block，因为 false 路径需要 continuation

对应文件：

- `src/ir/ir_builder.cpp`

### 6. `IRTextGen` 的 `store` 输出只支持常量和二元表达式

问题现象：

- 对 `load` 结果、比较结果等普通 SSA 值，`store` 的文本会打印不完整
- 例如 `store , @lor_tmp_1`

修复方式：

- `store` 统一改为使用 `print_value(store->value_, out)`
- 目标地址同样用 `print_value(store->dest_, out)`

对应文件：

- `src/ir/ir_text_gen.cpp`

### 7. RISC-V backend basic block 标签缺少冒号

问题现象：

- 生成的汇编形如：
  - `entry_0`
  - `then_1`
- 而不是合法的：
  - `entry_0:`
  - `then_1:`

这会直接导致汇编阶段失败。

修复方式：

- `emit_basic_block(...)` 输出标签时补上 `:`

对应文件：

- `src/back_end/riscv.cpp`

## 当前验证

已重点检查的内容：

- `if-else` 赋值后继续执行
- 嵌套 `if`，内层两边都 `return`
- `||` / `&&` 的短路 CFG 结构
- 复杂逻辑表达式初始化
- 综合逻辑用例中的 koopa 与 RISC-V 文本生成

重点确认的结果：

- 变量初始化中的逻辑表达式，其 `store` 已经落到 merge block
- `store` 的 koopa 文本不再出现空操作数
- `if-else` 两边都终结时，不再留下空 merge block
- RISC-V basic block 标签已经是合法的 `label:`

## 仍需注意

- 当前本地验证主要覆盖：
  - koopa 文本是否结构合法
  - riscv 文本是否至少在格式上合法
- 最终是否通过 Docker 侧完整测试，仍需以外部测试结果为准
