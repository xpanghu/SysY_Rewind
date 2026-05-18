# Compiler Module Flow

这份文档用两张较小的模块图描述当前 SysY 编译器的数据流。第一张图到 AST 为止，第二张图从 AST lowering 到 IR、RISC-V 和 baremetal 运行。

## 输入到 AST

```mermaid
flowchart LR
  A[SysY 源码<br/>input.sysy] --> B[项目入口<br/>main.cpp]
  B --> C[Driver Options<br/>解析 -ast / -koopa / -riscv]
  C --> D[Compilation Pipeline<br/>src/driver/pipeline.cpp]

  D --> E[Lexer<br/>sysy.l]
  E --> F[Parser<br/>sysy.y]
  F --> G[AST<br/>ast.h / ast.cpp]

  G --> H[-ast 输出<br/>AST Dump]
```

## AST 到 IR 和目标输出

```mermaid
flowchart LR
  A[AST<br/>ast.h / ast.cpp] --> B[Lowering<br/>RewindIRBuilder]

  B --> C[Semantic Helpers<br/>SymbolTable / ConstEval / ArrayInit / RuntimeDecls]
  C --> D[IR Core<br/>IRModule / IRFunction / IRBasicBlock / IRValue]
  B --> D

  D --> E[-koopa 输出<br/>IRTextGen]
  D --> F[-riscv 后端<br/>RISC-V Backend]
  F --> G[RISC-V 汇编<br/>hello.asm]
  G --> H[Runtime / Linker / QEMU<br/>baremetal 运行]
```

拆分边界放在 AST：前半部分强调前端和 driver 如何生成 AST，后半部分强调 AST 如何通过 lowering 进入 Rewind IR，并由 IR printer 或 RISC-V backend 消费。
