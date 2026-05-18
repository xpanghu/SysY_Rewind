# Project Structure

这份文档合并原来的项目组织和模块架构说明，从“目录结构”和“逻辑模块”两个角度描述当前项目。

## 仓库目录组织

```mermaid
flowchart TB
  Repo["SysY Compiler Repository"] --> Include["include<br/>公共头文件和跨模块接口"]
  Repo --> Src["src<br/>具体实现"]
  Repo --> Docs["docs<br/>项目讲解和架构档案"]
  Repo --> Tests["tests<br/>SysY 输入样例"]
  Repo --> ThirdParty["third_party<br/>SysY runtime"]
  Repo --> Build["build / debug / riscv32-baremetal<br/>生成物"]
  Repo --> Makefile["Makefile<br/>构建和运行入口"]

  Docs --> ProjectDocs["docs/project<br/>项目掌握和面试讲解"]
  Docs --> ArchitectureDocs["docs/architecture<br/>长期重构和演进规划"]
```

## 源码目录组织

```mermaid
flowchart TB
  Src["src"] --> Main["main.cpp<br/>程序入口"]
  Src --> Driver["driver<br/>命令行和阶段编排"]
  Src --> FrontEnd["front_end<br/>Flex / Bison / AST dump"]
  Src --> IR["ir<br/>IR core / lowering / printer"]
  Src --> BackEnd["back_end<br/>RISC-V 汇编生成"]

  IR --> Builder["ir_builder.cpp<br/>AST 到 IR 主 lowering"]
  IR --> ConstEval["ir_builder_const_eval.cpp<br/>常量表达式求值"]
  IR --> ArrayInit["ir_builder_init.cpp<br/>数组初始化展开"]
  IR --> RuntimeDecls["ir_builder_runtime.cpp<br/>SysY runtime 声明"]
  IR --> TextGen["ir_text_gen.cpp<br/>IR 文本输出"]
```

## 逻辑模块关系

```mermaid
flowchart TB
  Driver["Driver<br/>参数解析 / 文件 IO / 阶段编排"]
  FrontEnd["Front End<br/>Lexer / Parser / AST"]
  Lowering["Lowering<br/>AST -> Rewind IR"]
  Semantic["Semantic Helpers<br/>符号表 / 常量求值 / 数组初始化 / runtime 声明"]
  IRCore["IR Core<br/>Type / Value / Instruction / BasicBlock / Function / Module"]
  IRPrinter["IR Text Printer<br/>-koopa 输出"]
  Backend["RISC-V Backend<br/>栈帧 / 调用约定 / 指令选择 / 汇编输出"]
  Runtime["Runtime / Toolchain<br/>libsysy / linker / QEMU"]

  Driver --> FrontEnd
  Driver --> Lowering
  Driver --> IRPrinter
  Driver --> Backend

  Lowering --> FrontEnd
  Lowering --> Semantic
  Lowering --> IRCore
  Semantic --> IRCore

  IRPrinter --> IRCore
  Backend --> IRCore
  Runtime --> Backend
```

## 依赖方向

```mermaid
flowchart LR
  Source["SysY Source"] --> AST["AST"]
  AST --> IR["Rewind IR"]
  IR --> Output["IR Text / RISC-V"]

  FrontEnd["front_end<br/>只生产 AST"] -.-> AST
  Lowering["lowering<br/>桥接 AST 和 IR"] -.-> IR
  IRCore["ir core<br/>不依赖 AST"] -.-> IR
  Backend["backend<br/>只消费 IR"] -.-> Output
```

关键原则：

- `front_end` 不依赖 IR，只负责把源码变成 AST。
- `IR core` 不依赖 AST，只负责表达中间表示。
- `lowering` 是唯一同时理解 AST 和 IR 的桥接层。
- `backend` 只依赖稳定 IR，不直接依赖 parser 或 AST。
- `driver` 不承担具体编译逻辑，只负责阶段编排。

> 项目目录上按接口、实现、文档、runtime 和生成物分层；逻辑上按 driver、front end、lowering、IR core、IR printer、RISC-V backend 分层。核心设计是让 AST 和 IR 解耦，让 lowering 成为唯一桥接层，后端只消费稳定 IR。
