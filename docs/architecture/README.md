# SysY Compiler Architecture Notes

这个目录用于长期讨论和推进项目整体架构调整。

目标不是一次性重写整个编译器，而是先把当前链路读清楚，再把前端、中间 IR、后端、驱动和运行时脚本的职责边界固定下来。之后每次只选择一个小任务推进，确保编译链始终可验证。

如果目标是面试前快速掌握项目结构，建议先阅读 `../project/`。这个目录更关注长期重构记录和任务推进。

## 文档索引

- `target_architecture_and_tasks.md`: 项目长期方向 overview，记录目标结构、模块交互和 A1-A13 的大致工作内容。
- `task_progress.md`: 架构任务推进顺序和执行记录，记录当前下一步和已经完成的任务。
- `riscv_backend_design.md`: RISC-V 后端专项设计，记录组件边界、IR 指令支持表、最小 Machine IR 演进方案和后端重构时的最小验证方式。
- `ssa_mem2reg_design.md`: A11 专项设计，说明 Rewind IR 如何采用 basic block arguments 表达 SSA 合流，以及 `mem2reg` 需要补齐的 IR、分析和后端基础设施。
- `support_matrix.md`: 当前语法能力跨 parser/AST、lowering、IR text、RISC-V/baremetal 的重构支持边界表。
- `test_artifact_layout.md`: A8/A9 目录整理边界，说明测试资产、脚本入口、临时产物和 baremetal 产物各自放在哪里。

## 记录边界

- overview 文档只回答“项目往哪里走”。
- 专项设计文档回答“某一项任务具体怎么做”。
- 任务推进文档回答“现在做到哪里，下一步做什么”。

## 工作方式

1. 修改架构前，先更新或确认这里的文档。
2. 每轮只选择一个边界清晰的小任务。
3. 每个任务都要保留原有行为，并给出最小验证方式。
4. 前端、IR、后端之间通过明确的数据结构交互，避免跨层直接依赖实现细节。
