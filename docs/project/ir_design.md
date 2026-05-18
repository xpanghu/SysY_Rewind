# IR Design

这份文档聚焦 Rewind IR 的结构和 AST 到 IR 的 lowering 关系。

## IR 在项目中的位置

```mermaid
flowchart LR
  AST["AST<br/>源语言结构"] --> Lowering["Lowering<br/>语义显式化"]
  Lowering --> IR["Rewind IR<br/>前后端边界"]
  IR --> Printer["IR Text Printer<br/>-koopa 输出"]
  IR --> Backend["RISC-V Backend<br/>目标代码生成"]
```

Rewind IR 的作用不是复制 SysY 语法，而是把前端语义转换成更接近编译器中间层的表示，例如显式的函数、基本块、load/store、branch/jump、call、数组寻址和全局对象。

## IR 对象关系

```mermaid
flowchart TB
  Module["IRModule<br/>拥有全部 IR 对象"] --> Globals["Global Values<br/>全局变量 / 全局数组"]
  Module --> FuncDecls["Function Declarations<br/>runtime 函数声明"]
  Module --> Functions["IRFunction<br/>函数定义"]

  Functions --> Params["IRFuncArgRef<br/>形式参数引用"]
  Functions --> Blocks["IRBasicBlock<br/>基本块"]
  Blocks --> Insts["IR Instructions<br/>alloc / load / store / binary / call / branch / jump / ret"]
  Insts --> Values["IRValue<br/>统一值模型"]
  Values --> Types["IRType<br/>i32 / unit / pointer / array / function"]
```

核心设计点：

- `IRModule` 负责 IR 对象所有权和 factory。
- `IRValue` 是统一值模型，常量、参数引用和指令都可以作为 value。
- `IRInstruction` 继承自 `IRValue`，有结果的指令可以被后续指令引用。
- `IRType` 表达 `i32`、`unit`、pointer、array、function 等类型。
- `IRFunction` 包含参数和基本块，`IRBasicBlock` 包含指令序列。

## Lowering 上下文

```mermaid
flowchart TB
  Builder["RewindIRBuilder<br/>模块级 lowering 驱动"] --> ModuleSymbols["module_symbols_<br/>全局符号和函数"]
  Builder --> FuncContext["FuncContext<br/>函数内 lowering 状态"]

  FuncContext --> CurrentFunction["current_function"]
  FuncContext --> CurrentBlock["current_block"]
  FuncContext --> LocalSymbols["local symbols<br/>词法作用域"]
  FuncContext --> NameCounters["命名计数器<br/>percent / block name"]
  FuncContext --> LoopTargets["循环目标<br/>break / continue"]
```

当前 lowering 里最重要的边界是：

- module 层保存全局符号、全局变量、函数声明和函数定义。
- function 层保存当前函数、当前基本块、局部作用域、循环跳转目标和命名状态。
- `ConstEvaluator`、数组初始化、runtime 声明已经从主 builder 中拆出第一阶段。

## 典型 IR 显式化

```mermaid
flowchart TB
  Expr["表达式<br/>a + b * c"] --> Binary["IRBinaryInst<br/>显式二元运算"]
  Assign["赋值<br/>x = expr"] --> Store["store<br/>写入地址"]
  LVal["左值<br/>x 或 a[i]"] --> Address["地址 lowering<br/>alloc / global / getptr / getelemptr"]
  Read["右值读取<br/>使用 x"] --> Load["load<br/>从地址读取"]
  If["if / while"] --> CFG["basic block + branch / jump"]
  Call["函数调用"] --> IRCall["IRCallInst<br/>callee + args + result type"]
```

面试时可以这样总结：

> Rewind IR 参考 LLVM 的 Value 思想，用统一的 value/type/instruction 模型承接 AST lowering 和后端代码生成。它当前还不是完整 SSA，但已经具备函数、基本块、显式控制流和指令值模型，后续可以继续扩展 Verifier、Pass Manager、SSA 和 mem2reg。
