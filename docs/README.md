# SysY Compiler Docs

这个目录分成两类文档：

- `project/`: 面向项目掌握和面试讲解，重点描述项目组织、模块划分、依赖关系和数据流。文档尽量使用 Mermaid 图配合文字说明。
- `architecture/`: 面向长期重构和演进规划，记录当前架构基线、支持边界表、任务拆分和重构路线。

建议阅读顺序：

```mermaid
flowchart LR
  A["项目总览<br/>project/README.md"] --> B["项目结构<br/>project/project_structure.md"]
  B --> C["前端设计<br/>project/frontend_design.md"]
  C --> D["IR 设计<br/>project/ir_design.md"]
  D --> E["后端设计<br/>project/backend_design.md"]
  E --> F["编译数据流<br/>project/compilation_flow.md"]
  F --> G["长期重构档案<br/>architecture/"]
```

如果目标是准备面试，优先阅读 `project/`。如果目标是继续推进代码重构，优先阅读 `architecture/`。
