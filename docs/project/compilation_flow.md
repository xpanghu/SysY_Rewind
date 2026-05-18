# Compilation Data Flow

这份文档从一份 SysY 输入出发，说明数据如何在项目中流动。

## 前端数据流

```mermaid
flowchart LR
  A[SysY 源码<br/>input.sysy] --> B[main.cpp<br/>项目入口]
  B --> C[Driver Options<br/>解析编译模式]
  C --> D[Compilation Pipeline<br/>阶段编排]

  D --> E[Lexer<br/>sysy.l]
  E --> F[Token Stream<br/>关键字 / 标识符 / 数字 / 运算符]
  F --> G[Parser<br/>sysy.y]
  G --> H[AST<br/>语法树]
  H --> I[-ast<br/>AST Dump]
```

## AST 到 IR

```mermaid
flowchart LR
  AST[AST<br/>CompUnit / Decl / FuncDef / Stmt / Exp] --> Builder[RewindIRBuilder<br/>主 lowering]

  Builder --> Symbols[SymbolTable<br/>作用域 / 变量 / 常量 / 函数]
  Builder --> ConstEval[ConstEvaluator<br/>ConstExp 求值]
  Builder --> ArrayInit[ArrayInit<br/>数组初始化展开]
  Builder --> RuntimeDecls[RuntimeDecls<br/>SysY 库函数声明]

  Symbols --> IRModule[IRModule<br/>IR 所有权容器]
  ConstEval --> IRModule
  ArrayInit --> IRModule
  RuntimeDecls --> IRModule
  Builder --> IRModule
```

## IR 到输出

```mermaid
flowchart LR
  IR[IRModule<br/>函数 / 基本块 / 指令 / 全局对象] --> Mode{输出模式}

  Mode --> Koopa[-koopa]
  Koopa --> IRText[IRTextGen<br/>打印 IR 文本]
  IRText --> KoopaFile[hello.koopa]

  Mode --> Riscv[-riscv]
  Riscv --> Backend[RISC-V Backend]
  Backend --> Asm[hello.asm]
  Asm --> Link[Runtime / Linker]
  Link --> Elf[baremetal ELF]
  Elf --> QEMU[QEMU 运行]
```

## 典型语义数据流

```mermaid
flowchart TB
  LVal["数组访问<br/>a[i][j]"] --> Lookup["查符号表<br/>确认 a 的存储和值类型"]
  Lookup --> Index["lower 下标表达式<br/>i / j"]
  Index --> Ptr["getptr / getelemptr<br/>计算元素地址"]
  Ptr --> Access{"读还是写"}
  Access --> Load["load<br/>读取元素值"]
  Access --> Store["store<br/>写入元素值"]
  Load --> BackendLoad["RISC-V 地址计算和 lw"]
  Store --> BackendStore["RISC-V 地址计算和 sw"]
```

面试时可以这样总结：

> 数据流上，源码先进入 front end 生成 AST；AST 经过 lowering 进入 Rewind IR；后续 `-koopa` 和 `-riscv` 都只消费 IR。数组、函数调用、控制流等复杂语义都在 AST 到 IR 的阶段被显式化，后端只需要按 IR 指令完成目标相关翻译。
