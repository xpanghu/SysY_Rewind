# Current Pipeline Analysis

记录时间：2026-04-25。

这份文档描述当前项目中前端、中间 IR、后端、驱动和运行时脚本的实际交互方式。它是后续架构优化的基线。

## 当前主流程

入口在 `src/main.cpp`，实际编排逻辑在 `src/driver/pipeline.cpp`。

当前编译流程：

```text
input.sysy
  -> Driver options: include/driver/options.h, src/driver/options.cpp
  -> Driver pipeline: include/driver/pipeline.h, src/driver/pipeline.cpp
  -> Flex lexer: src/front_end/sysy.l
  -> Bison parser: src/front_end/sysy.y
  -> AST: include/front_end/ast.h, src/front_end/ast.cpp
  -> RewindIRBuilder: include/ir/ir_builder.h, src/ir/ir_builder.cpp
  -> IRModule: include/ir/rewind_ir.h
  -> output mode:
       -ast   -> AST Dump
       -koopa -> IRTextGen
       -riscv -> riscv::emit_module
```

`main.cpp` 现在只负责调用 `sysy::driver::run`。命令行解析、文件打开、parser 调用、AST 到 IR 的转换、输出模式分发已经移入 driver 层。

## 前端现状

主要文件：

- `src/front_end/sysy.l`: 词法分析，识别关键字、标识符、整数、运算符、注释和空白。
- `src/front_end/sysy.y`: 语法分析，并在 grammar action 中直接构造 AST。
- `include/front_end/ast.h`: AST 节点定义。
- `src/front_end/ast.cpp`: AST Dump 实现。

当前职责：

- 解析 SysY 源码。
- 生成 AST。
- 支持 `-ast` 文本输出。

已发现问题：

- `ast.h` 已不再 include `rewind_ir.h`。前端头文件已经完成第一步 IR 解耦。
- AST 节点都继承 `BaseAST`，大量字段使用 `std::unique_ptr<BaseAST>`，后续 lowering 依赖 `dynamic_cast` 和 `expect_node` 检查具体类型。
- AST Dump 放在 AST 节点类内部，调试输出和语法树数据结构耦合。

## 中间 IR 现状

主要文件：

- `include/ir/rewind_ir.h`: IR value、instruction、basic block、function、module 的核心定义。
- `include/ir/ir_type.h`, `src/ir/ir_type.cpp`: IR 类型系统和 `IRTypeContext`。
- `include/ir/symbol_table.h`, `src/ir/symbol_table.cpp`: 符号表。
- `include/ir/func_context.h`: 函数 lowering 上下文、作用域、循环目标、命名计数器。
- `include/ir/ir_builder.h`, `src/ir/ir_builder.cpp`: AST 到 IR 的主 lowering。
- `src/ir/ir_builder_init.cpp`: 数组初始化展开和 aggregate/local store sink。
- `src/ir/ir_builder_runtime.cpp`: SysY runtime 函数声明。
- `src/ir/ir_builder_const_eval.cpp`: 常量表达式求值。
- `include/ir/ir_text_gen.h`, `src/ir/ir_text_gen.cpp`: IR 文本输出。

当前职责：

- IR 数据模型。
- 类型创建和类型大小计算。
- AST lowering。
- 常量表达式求值。
- 符号表和作用域维护。
- 库函数声明。
- 数组初始化展开。
- 控制流基本块生成。
- IR 文本输出。

已发现问题：

- `RewindIRBuilder` 职责仍然偏宽。数组初始化、运行时函数声明和常量求值已经先按文件拆出第一阶段，但符号解析、语义检查、控制流构造和 IR 指令生成仍然集中在 builder 入口和成员函数中。
- `symbol_table.h` 已不再 include `ast.h`，当前符号表接口只依赖字符串、常量和 IR 前向声明。
- `rewind_ir.h` 是单个大头文件，包含 IR 类型、指令、module factory、所有 inline 创建逻辑。修改任何 IR 细节都容易触发大范围重编译和跨层依赖。
- `IRTypeContext` 里存在 `getTypeSize` 和 `getTypeAlign`，这是目标机器相关的数据布局逻辑。长期看，数据布局应该属于 target/backend 或独立 `DataLayout`，而不是纯 IR 类型系统。
- IR 已经包含 `call`、`branch`、`jump`、`global`、`getptr`、`getelemptr` 等能力，但后端和测试边界仍需要明确哪些是已验证支持，哪些只是 IR 层已有表达。

## 后端现状

主要文件：

- `include/back_end/riscv.h`
- `src/back_end/riscv.cpp`

当前职责：

- 从 `rewind_ir::IRModule` 直接生成 RISC-V 汇编。
- `FunctionFrame` 预扫描函数，分配栈帧、对象槽和值槽。
- `IREmitter` 遍历 IR，生成全局数据、函数、基本块和指令汇编。
- 当前策略仍偏向栈帧模型，值会物化到临时寄存器后再 spill。

已发现问题：

- RISC-V 后端直接读取 IR 节点内部字段，例如 `kind_`、`type_`、`name_`、各类指令字段。这是可以接受的第一阶段实现，但长期应通过更清晰的 IR API 或 visitor 降低脆弱性。
- `riscv.cpp` 同时包含栈帧布局、调用约定、全局变量输出、指令选择、汇编文本输出、标签清洗等多类职责。
- 后端依赖 IR 名称格式生成标签和符号，需要明确哪些命名规则由 IR 保证，哪些由后端负责 sanitize。
- 后端与 `IRTypeContext::getTypeSize/getTypeAlign` 存在隐含目标耦合。

## 驱动和运行时现状

主要文件：

- `src/main.cpp`
- `Makefile`
- `scripts/run_riscv_baremetal.sh`
- `third_party/sysyrt/baremetal/*`

当前职责：

- `main.cpp` 驱动所有编译模式。
- `Makefile` 提供构建、RISC-V 汇编生成、baremetal ELF 链接和 QEMU 运行入口。
- `run_riscv_baremetal.sh` 只保留为兼容包装器，实际逻辑已经迁移到 `Makefile`。
- `third_party/sysyrt/baremetal/sysy_baremetal.c` 提供 SysY runtime 函数。

已发现问题：

- `main.cpp` 已经不再使用 `assert(argc == 5)`。基础命令行校验在 `driver/options` 中完成。
- 编译模式分发已经从 `main.cpp` 移到 `driver/pipeline`，后续新增 pass 或 target 应优先扩展 driver 层。
- baremetal runtime 中间产物仍在 `build/sysyrt/riscv32-baremetal`，最终 RISC-V 汇编和 ELF 输出到项目根目录下的 `riscv32-baremetal/<input-name>/`。

## 初步结论

当前项目已经具备完整编译链：

```text
SysY source -> AST -> Rewind IR -> IR text / RISC-V asm -> baremetal ELF
```

后续架构优化的重点不是推倒重写，而是拆清职责：

- 前端只负责源码到 AST。
- 语义和 lowering 层负责 AST 到 IR。
- IR core 只负责数据模型、所有权和验证。
- IR printer 只负责文本输出。
- 后端只负责目标相关 lowering 和汇编生成。
- driver 只负责命令行、阶段编排、错误报告。
