# SysY Compiler Architecture Notes

这个目录用于长期讨论和推进项目整体架构调整。

目标不是一次性重写整个编译器，而是先把当前链路读清楚，再把前端、中间 IR、后端、驱动和运行时脚本的职责边界固定下来。之后每次只选择一个小任务推进，确保编译链始终可验证。

## 文档索引

- `current_pipeline.md`: 当前代码链路、模块职责和已发现的耦合点。
- `support_matrix.md`: 当前语法能力跨 parser/AST、lowering、IR text、RISC-V/baremetal 的重构支持边界表。
- `target_architecture_and_tasks.md`: 建议的优化架构、模块交互方式和长期任务拆分。

## 工作方式

1. 修改架构前，先更新或确认这里的文档。
2. 每轮只选择一个边界清晰的小任务。
3. 每个任务都要保留原有行为，并给出最小验证方式。
4. 前端、IR、后端之间通过明确的数据结构交互，避免跨层直接依赖实现细节。
