# Test And Artifact Layout

这份文档收敛 A8/A9 的目录整理边界。当前目标不是引入完整测试框架，而是先把测试资产、自动化脚本和生成物分开，保证后续继续补 smoke/golden tests 时目录语义稳定。

## 目录职责

```text
tests/                    # Git 跟踪的测试资产
  smoke/                  # Makefile 和 smoke 脚本使用的基础 SysY 输入
  fixtures/awesome-sysy/  # awesome-sysy 程序的稳定 stdin fixture
  awesome-sysy/           # 外部示例 SysY 程序

scripts/                  # Git 跟踪的开发和回归脚本入口

tmp/                      # Git 忽略的临时输出
  manual/                 # make run-ast/run-koopa/run-riscv 的手工调试输出
  regression-smoke/       # 回归 smoke 输出
  semantic-smoke/         # 语义 smoke 输出
  ir-verifier-smoke/      # IR verifier smoke 输出
  cfg-analysis-smoke/     # CFG analysis smoke 输出
  dominance-analysis-smoke/ # dominance analysis smoke 输出
  ir-rewrite-smoke/       # IR rewrite smoke 输出
  mem2reg-smoke/          # Mem2Reg pass smoke 输出和可视化 IR 文本
  ssa-smoke/              # -ssa compile mode 的真实前端链路输出

build/                    # 构建树和 baremetal runtime 中间产物
riscv32-baremetal/        # baremetal 汇编与 ELF 最终产物
```

边界规则：

- `tests/` 保存会随项目版本演进的测试输入、fixture 和未来的 golden 文件。
- `scripts/` 保存自动化入口，不保存输入资产和输出产物。
- `tmp/` 只保存可再生输出或手工 scratch，不保存回归链路依赖的稳定输入。
- `debug/` 不再作为独立输出目录；手工编译输出统一进入 `tmp/manual/`。
- `riscv32-baremetal/` 保留最终 `.s` 和 `.elf`，便于 QEMU 手工复跑。
- `make clean-tmp` 单独清理 `tmp/`，避免 `make clean` 顺手删掉手工对照中的输出。

## A8/A9 执行清单

### 1. 固定资产边界

- [x] 从 `.gitignore` 移除 `tests/`，让测试资产可以跟随版本管理。
- [x] 保持 `tmp/`、`build/`、`riscv32-baremetal/` 为生成物目录。
- [x] 取消 `debug/` 输出入口，避免和 `tmp/` 承担相同职责。

### 2. 迁移当前测试资产

- [x] 将基础 smoke 输入移动到 `tests/smoke/hello.sysy`。
- [x] 将已验证的 `awesome-sysy` stdin 样例移动到 `tests/fixtures/awesome-sysy/`：
  `maze.in`、`lisp-simple.in`、`lisp-fib.in`。
- [x] 暂时保留手工输出和未进入回归链路的临时文件在 `tmp/`。

### 3. 更新入口

- [x] 让 `Makefile` 的默认简单输入指向 `tests/smoke/hello.sysy`。
- [x] 让 `make run-ast`、`make run-koopa`、`make run-riscv` 输出到 `tmp/manual/`。
- [x] 让 smoke 脚本和文档引用新的 smoke/fixture 路径。

### 4. 回写文档

- [x] 在架构任务文档记录 A8/A9 本轮目录整理结果。
- [x] 更新 `awesome-sysy` 和后端说明中的示例路径。
- [x] 保持 `tmp/` 示例只表达“输出或临时 scratch”，不再把稳定 fixture 写进 `tmp/`。

### 5. 验收

- [x] `make build`
- [x] `make run-ast`
- [x] `make run-koopa`
- [x] `make run-riscv`
- [x] `make regression-smoke`
- [x] `make semantic-smoke`
- [x] `make ir-verifier-smoke`
- [x] `make cfg-analysis-smoke`
- [x] `make dominance-analysis-smoke`
- [x] `make ir-rewrite-smoke`
- [x] `make mem2reg-smoke`
- [x] `make ssa-smoke`
- [x] 工具链可用时，使用 `tests/fixtures/awesome-sysy/lisp-simple.in` 跑一次 baremetal `lisp.c`。

## 后续扩展

当 golden tests 真正落地后，再按资产类型补目录：

```text
tests/golden/ast/
tests/golden/ir/
tests/golden/riscv/
```

在此之前不额外拆 `scripts/test/`、`scripts/run/`，避免目录层级比现有脚本数量更复杂。
