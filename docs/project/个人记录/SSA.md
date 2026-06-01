# SSA 和 Mem2Reg 面试讲解

这份文档记录我在面试中如何讲解当前项目的 A11 任务：为什么要做 SSA，为什么需要
`mem2reg`，以及当前编译器是如何从普通 IR 转换到 SSA 形式的。

先纠正一个容易混淆的说法：不是“SSA IR 到 mem2reg”，而是 **Mem2RegPass 把当前
memory-form IR 转换成 SSA IR**。

当前前端 lowering 仍然先生成比较朴素、容易保证正确的内存形式：

```text
@a_1 = alloc i32
store 1, @a_1
%0 = load @a_1
ret %0
```

`mem2reg` 的作用是把这类局部标量变量从 `alloc/load/store` 形式提升成 SSA value：

```text
ret 1
```

所以 A11 的核心目标不是重写整个前端，而是在现有 `AST -> IR` 后面加一层 pass：

```text
SysY 源码 -> AST -> memory-form IR -> Mem2RegPass -> SSA IR
```


## 1. 先用一句话解释 SSA

SSA 是 Static Single Assignment 的缩写，可以理解为：**在 IR 层，每一个 value 只被定义一次**。

这句话对新手来说容易误解。它不是说源代码里的变量不能重新赋值，而是说编译器会把多次赋值拆成多个不同的 IR value。

比如源代码：

```c
int a = 1;
a = a + 1;
return a;
```

在源代码里只有一个变量 `a`，但是在 SSA 视角里可以理解成：

```text
%a0 = 1
%a1 = add %a0, 1
ret %a1
```

这里 `%a0` 和 `%a1` 都只被定义一次。这样后续做优化会更简单，因为每个 value 的来源非常明确。


## 2. 为什么当前项目一开始没有直接生成 SSA

因为直接从 AST lowering 到 SSA 会让前端复杂很多。

源语言里变量是可以反复赋值的，例如：

```c
int a;
if (cond) {
  a = 1;
} else {
  a = 2;
}
return a;
```

如果前端直接生成 SSA，就必须在 lowering `if/else` 时立刻判断：`return a` 到底应该使用 then 分支的值，还是 else 分支的值。这会让 AST lowering 同时处理语义、控制流、变量版本管理和合流点插入，代码会很容易变乱。

所以当前项目先选择更稳的做法：前端把局部变量 lowering 成内存槽。

```text
@a_1 = alloc i32

%then:
  store 1, @a_1
  jump %merge

%else:
  store 2, @a_1
  jump %merge

%merge:
  %0 = load @a_1
  ret %0
```

这个形式容易生成，也容易和源代码对应。后续再通过 `Mem2RegPass` 把它提升成 SSA。


## 3. 控制流合流时为什么需要 Phi 或 Block Arguments

最关键的问题出现在控制流合流点。

继续看这个例子：

```c
int a;
if (cond) {
  a = 1;
} else {
  a = 2;
}
return a;
```

在 `%merge` 这个基本块里，`a` 的值可能来自 then，也可能来自 else。传统 SSA 会用 Phi 表示：

```text
%merge:
  %a = phi [1, %then], [2, %else]
  ret %a
```

当前项目没有引入单独的 `IRPhiInst`，而是采用 **Basic Block Arguments** 表示同一件事：

```text
%then:
  jump %merge(1)

%else:
  jump %merge(2)

%merge(%a_1_phi_0: i32):
  ret %a_1_phi_0
```

这两种表示在语义上等价。区别只是表达形式不同：

- Phi 形式：合流块里显式写一条 `phi` 指令。
- Block Arguments 形式：合流块带参数，前驱跳转时把值传过去。

我当前项目选择 block arguments，是因为它把“哪条边传来哪个值”直接挂在控制流边上，更直观，也方便后续 Machine IR 用 edge-copy / parallel-copy 表示。


## 4. A11 任务到底做了什么

A11 不是一个单独函数，而是一组基础设施。可以按下面顺序理解。

### A11.1 IR 支持 Block Arguments

第一步是让 IR 本身能表达这种形式：

```text
%merge(%a_1_phi_0: i32):
  ret %a_1_phi_0
```

这需要：

- 给 `IRBasicBlock` 增加参数列表。
- 增加 `IRBlockArgRef`，让基本块参数也能作为 `IRValue` 被使用。
- 给 `jump` 和 `branch` 增加 edge arguments。
- 修改 IR printer，让它能打印 `%merge(%x: i32)` 和 `jump %merge(value)`。
- 修改 verifier，检查边上传参数量和类型是否匹配目标块参数。

这一步只是让 IR “能表示 SSA 合流”，还没有自动把普通变量转换成 SSA。

### A11.2 构建 CFG

`mem2reg` 需要知道基本块之间怎么流动，所以需要 CFGAnalysis。

CFG 是 Control Flow Graph，可以理解为：

```text
entry -> then -> merge
entry -> else -> merge
```

它回答两个问题：

- 一个 block 可以跳到哪些 successor？
- 一个 block 有哪些 predecessor？

没有 CFG，就无法知道 `%merge` 是 then 和 else 的合流点。

### A11.3 计算 Dominator 和 Dominance Frontier

这一步名字很吓人，但可以拆开理解。

**Dominator 支配关系**：如果从函数入口到 block B 的所有路径都必须经过 block A，那么 A 支配 B。

例如：

```text
entry -> then -> merge
entry -> else -> merge
```

`entry` 支配所有块，因为任何路径都从 `entry` 开始。但是 `then` 不支配 `merge`，因为还可以从 `else` 到达 `merge`。

**Dominance Frontier 支配边界**：一个变量如果在多个分支中被定义，那么需要在这些分支重新汇合的位置放合流参数。这个“重新汇合的位置”通常可以通过 dominance frontier 找到。

面试里可以用一句话讲：

> CFG 告诉我控制流怎么走，dominance 告诉我一个定义在哪些位置必然可见，dominance frontier 告诉我应该在哪些合流块插入 block argument。

### A11.4 提供 IR Rewrite 能力

`mem2reg` 不只是添加 block argument，还要删除旧的内存形式：

```text
alloc / load / store
```

所以需要一层统一的 IR 改写工具：

- 替换某个 value 的所有 use。
- 遍历指令 operands。
- 从 basic block 里移除已经不需要的指令。

这样后续不管是 mem2reg，还是常量传播、死代码删除，都不用每个 pass 手写一套替换逻辑。

### A11.5 实现 Mem2RegPass

这一步才是真正把变量提升到 SSA。

当前实现的核心过程是：

1. 找到可以提升的局部 `alloc i32`。
2. 收集这个变量在哪些 block 里被 `store` 定义。
3. 根据 dominance frontier 找到需要添加 block argument 的合流块。
4. 沿着 dominator tree 从入口开始重命名变量。
5. 遇到 `store`，更新“当前变量值”。
6. 遇到 `load`，用当前变量值替换这个 load。
7. 给前驱边补上传给目标 block argument 的 incoming value。
8. 删除已经被提升掉的 `alloc/load/store`。


## 5. 具体例子：if/else 如何转换

源代码：

```c
int main() {
  int a;
  if (1) {
    a = 1 + 1;
  } else {
    a = 1 + 2;
  }
  return a;
}
```

前端先生成 memory-form IR，大致是：

```text
@a_1 = alloc i32

%then:
  %3 = add 1, 1
  store %3, @a_1
  jump %merge

%else:
  %5 = add 1, 2
  store %5, @a_1
  jump %merge

%merge:
  %6 = load @a_1
  ret %6
```

`mem2reg` 后变成：

```text
%then:
  %3 = add 1, 1
  jump %merge(%3)

%else:
  %5 = add 1, 2
  jump %merge(%5)

%merge(%a_1_phi_0: i32):
  ret %a_1_phi_0
```

这里 `%a_1_phi_0` 的含义不是源代码中真的有一个新变量，而是：

- `a_1`：来自原来的局部变量存储对象 `@a_1`。
- `phi`：说明它是控制流合流后的 SSA 参数。
- `0`：当前 pass 生成的 block argument 编号，用来保证名称唯一。


## 6. 具体例子：while 为什么也需要合流参数

循环比 if/else 更容易暴露 SSA 的价值。

源代码：

```c
int main() {
  int i = 0;
  while (i < 3) {
    i = i + 1;
  }
  return i;
}
```

循环头 `%while_entry` 的 `i` 有两个来源：

- 第一次进入循环时，来自 entry 的初始值 `0`。
- 每次循环体执行完后，来自 backedge 的更新值 `%next`。

所以 SSA 形式会变成：

```text
%entry:
  jump %while_entry(0)

%while_entry(%i_1_phi_0: i32):
  %cond = lt %i_1_phi_0, 3
  br %cond, %while_body, %end

%while_body:
  %next = add %i_1_phi_0, 1
  jump %while_entry(%next)

%end:
  ret %i_1_phi_0
```

这个例子可以很好地说明：SSA 不是只处理 `if/else`，循环回边同样需要合流值。


## 7. 当前实现的边界

面试中不要把当前实现说得过满。更好的说法是：当前完成了第一版标量 mem2reg。

当前支持：

- 局部标量 `alloc i32`。
- 直接 `load/store` 使用。
- `if/else` 合流。
- `while` 回边。
- 通过 `-ssa` 模式从真实 SysY 源码验证 SSA IR 输出。

当前暂时不提升：

- 数组。
- 指针和地址逃逸。
- 被 `getptr/getelemptr` 派生出来的地址。
- 作为函数参数传出去的地址。
- 可能读取未初始化值的局部变量。

这样说反而更可信：我不是声称完成了工业级完整 SSA，而是明确知道当前 pass 的安全边界。


## 8. 面试中可以怎么讲

如果面试官问：“你们项目里的 SSA / mem2reg 是怎么做的？”

可以这样回答：

> 我当前项目的前端 lowering 并不是直接生成 SSA，而是先生成比较朴素的 memory-form IR。局部变量会被 lowering 成 `alloc`，赋值是 `store`，读取是 `load`。这样做的好处是前端实现比较稳定，不需要在 AST lowering 时立刻处理复杂的控制流合流。
>
> 后续我在 A11 中补了一套 SSA 基础设施，用 `mem2reg` 把可提升的局部标量从 `alloc/load/store` 形式转换成 SSA value。这个过程需要几部分能力：首先 IR 要支持 basic block arguments，也就是用 `%merge(%x: i32)` 这种形式表达 Phi 等价语义；其次需要 CFG 分析知道 basic block 的前驱和后继；然后需要 dominance 和 dominance frontier 决定在哪些合流块添加 block argument；最后需要 IR rewrite 能力，把旧的 load use 替换成 SSA value，并删除被提升掉的 alloc/load/store。
>
> 我没有使用传统单独的 `phi` 指令，而是采用 block arguments。比如 then 分支跳转时写 `jump %merge(%x_then)`，else 分支写 `jump %merge(%x_else)`，merge block 写 `%merge(%x: i32)`。这和 Phi 在语义上等价，但 incoming value 直接挂在控制流边上，我觉得更直观，也方便后续下降到 Machine IR 时转换成 edge-copy 或 parallel-copy。
>
> 当前这版 mem2reg 是保守实现，只提升局部、非逃逸、直接 load/store 使用的 `alloc i32`。数组、指针、地址逃逸和未初始化读取都先跳过，保证正确性优先。为了验证它不是手写 IR，我加了 `-ssa` 模式，从真实 SysY 源码经过 parser、AST lowering、memory-form IR，再跑 Mem2RegPass，最后输出 SSA IR 文本。


## 9. 如果面试官继续追问

### 为什么不直接在 AST lowering 阶段生成 SSA？

可以回答：

> 直接生成 SSA 当然可以，但会让前端 lowering 复杂很多。因为 lowering 时不仅要处理语法结构，还要维护每个变量在不同控制流路径上的当前版本，并且在 if/while 合流点插入 Phi 或 block argument。我的项目先用 memory-form IR 保证前端正确性，再用 mem2reg pass 做 SSA 化，这更接近“先生成简单 IR，再通过 pass 优化和规范化”的路线，也更适合后续继续扩展优化 pass。

### Phi 和 block arguments 有什么区别？

可以回答：

> 它们表达能力等价。Phi 是在合流块内部写一条特殊指令，声明这个值来自哪个前驱；block arguments 是让合流块带参数，前驱跳转时把参数传过去。我的项目采用 block arguments，因为它把 incoming value 明确放在控制流边上，后续做边上的 copy lowering 时更自然。

### Dominance frontier 为什么需要？

可以回答：

> 因为一个变量如果在多个分支中被定义，就需要在这些定义重新汇合的位置创建合流参数。dominance frontier 正好描述了“某个定义支配关系开始失效、多个路径可能汇合”的边界，所以 mem2reg 可以用它决定在哪些 block 添加 block argument。

### 如何保证转换正确？

可以回答：

> 我做了几层保护。第一，Mem2RegPass 只处理安全的局部标量，不处理数组和逃逸地址。第二，IRVerifier 会检查 block argument 和 edge argument 的数量、类型、所属函数等结构约束。第三，项目里有 `make mem2reg-smoke` 验证手工 IR 形状，也有 `make ssa-smoke` 从真实 SysY 源码验证 `AST -> IR -> mem2reg -> SSA IR` 的完整链路。


## 10. 自己复盘时的核心记忆点

最重要的不是记住算法名，而是记住这条因果链：

```text
局部变量用 alloc/load/store 容易 lowering
        |
        v
memory-form IR 不利于优化
        |
        v
mem2reg 把可提升变量变成 SSA value
        |
        v
if/else 和 while 需要在合流点表达“值从哪条边来”
        |
        v
传统方案是 Phi，当前项目用 block arguments
        |
        v
实现上需要 CFG、dominance frontier、IR rewrite、verifier
```

面试时只要把这条线讲顺，A11 这部分就会比较清楚。
