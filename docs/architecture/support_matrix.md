# Support Matrix

记录时间：2026-05-03。

这份表是 A0.2 的重构支持边界表。它不是一张“编译器是否还能跑”的成绩表，而是一张拆分代码时使用的地图：每个 SysY 能力在哪个阶段被表达、被转换、被打印或被执行。

当前编译器已经通过大量 `-koopa` / `-riscv` / baremetal 测试，但重构时仍然需要这张表，因为“测试通过”和“模块边界清楚”不是同一件事。比如数组形参涉及 parser 语法、AST 节点、函数类型、pointer decay、`getptr/getelemptr`、调用约定和后端地址计算。以后如果只移动其中一块代码，这张表能提醒我们它真正牵连了哪些阶段。

## 状态含义

- `yes`: 当前阶段有明确实现，并且属于该阶段的责任。
- `verified`: 当前阶段不只是有实现，还已经被回归测试覆盖。
- `runtime`: 语义来自 SysY runtime 或 baremetal runtime。
- `limited`: 当前实现可用，但有已知边界或目标不是完整 SysY/C 语义。
- `n/a`: 该能力和这个阶段没有直接关系。
- `no`: 当前明确不支持，或有意不支持。

## 如何使用

重构前先看两列：

- `Boundary`: 这个能力跨越了哪些模块边界。
- `Regression anchor`: 改到相关模块后，至少应该跑哪些验证。

这样做的目的不是增加文档负担，而是避免重构时出现“parser 能过、IR 能打印、但后端或 runtime 语义悄悄变了”的隐性回退。

## 支持边界表

| Feature | Parser/AST | Semantic/Lowering | IR Text | RISC-V/Baremetal | Boundary | Regression anchor |
| --- | --- | --- | --- | --- | --- | --- |
| integer literal and scalar expression | verified | verified | verified | verified | AST expression -> IR value -> backend arithmetic | lv1-lv4, lvX arithmetic cases |
| unary `+ - !` and chained unary | verified | verified | verified | verified | expression lowering and const eval must stay consistent | focused tests for `!!x`, `- -1` |
| binary arithmetic and comparison | verified | verified | verified | verified | `BinaryOp` mapping, const eval, `IRBinaryInst`, backend emitter | lvX arithmetic/comparison cases |
| logical && ||  | verified | verified | verified | verified | parser expression tree, short-circuit lowering, block control flow | lvX short-circuit cases |
| local scalar variable | verified | verified | verified | verified | symbol table, `alloc/store/load`, stack frame slot | lv8/lvX scalar cases |
| local scalar const | verified | verified | n/a | n/a | const symbol lookup and `eval_*`; no runtime storage needed | const expression cases |
| global scalar variable | verified | verified | verified | verified | module symbol, `IRGlobalAllocInst`, global data emission | lv8/lv9 globals |
| global scalar const | verified | verified | verified | verified | const expression folding plus optional global storage when addressable | lv9/global const cases |
| local array definition | verified | verified | verified | verified | AST dimensions, const dimension eval, stack object layout | lv9 arrays, lvX array cases |
| global array definition | verified | verified | verified | verified | global initializer, data emission, symbol address | lv9/global array, lvX array cases |
| array initializer brace elision | verified | verified | verified | verified | `InitValAST/ConstInitValAST`, `InitTree`, aggregate/local stores | lv9 `04_arr_init_nd`, lvX array cases |
| local non-const array runtime initializer | verified | verified | verified | verified | runtime `lower_exp` leaves, local element stores | custom `int a[3] = {1,2,b}`, lvX local init |
| constexpr array initializer | verified | verified | verified | verified | `eval_exp` leaves, aggregate construction | global arrays, const arrays |
| array element read/write | verified | verified | verified | verified | lvalue address lowering, `getelemptr`, load/store, backend address math | lv9 access/loop/sort cases |
| array function parameter | verified | verified | verified | verified | function type, pointer decay, parameter slot, calling convention | lv9 arr params, lvX many array cases |
| scalar function parameter | verified | verified | verified | verified | function type, `IRFuncArgRef`, a0-a7 and stack args | lv8/lvX many params |
| more than 8 function arguments | verified | verified | verified | verified | call lowering, outgoing arg area, callee stack arg load | `many_parameters*`, lvX many params |
| function call and return value | verified | verified | verified | verified | call instruction, return type, `a0` return convention | lv8/lvX call cases |
| void function and void return | verified | verified | verified | verified | `unit` type, no result slot for void call, bare `ret` | lvX void function cases |
| `if/else` | verified | verified | verified | verified | branch blocks, labels, backend label uniqueness | lvX if cases |
| `while`, `break`, `continue` | verified | verified | verified | verified | loop target stack, branch/jump lowering | lvX loop cases |
| SysY input/output functions | runtime | verified | verified | verified | library declarations, runtime symbols, UART baremetal implementation | lv9 arr lib funcs, baremetal IO smoke |
| `starttime/stoptime` | runtime | verified | verified | limited | declarations exist; baremetal implementation is currently a placeholder | perf compatibility, no timing guarantee |
| large local stack frame | n/a | verified | verified | verified | frame layout and large offset load/store fallback | `radix_sort`, long array cases |
| global data and large arrays | verified | verified | verified | verified | IR global values, assembler data, memory layout | lv9/lvX global array cases |

## 有意不支持或受限的能力

| Case | Status | Reason |
| --- | --- | --- |
| variable length array, e.g. `int n = 3; int a[n];` | no | grammar uses `ConstExp` for array dimensions; runtime VLA is outside current SysY subset |
| using const array element as constexpr, e.g. `const int a[2] = {1,2}; const int x = a[1];` | no | array object is storage, not a scalar constexpr symbol in current semantic model |
| treating bare array as scalar value, e.g. `int c = a;` | no | array-to-pointer decay is only allowed in call argument/address contexts |
| full timing semantics for `starttime/stoptime` on baremetal | limited | local runtime keeps compatibility but does not provide real timing measurement |

## 当前验证集合

当前建议以这些命令作为重构保护：

```bash
cmake --build build -j4
docker run -i --rm -v /Users/qingxuliang/project/compiler/SysY:/root/compiler maxxing/compiler-dev autotest -koopa -s lv9 /root/compiler
docker run -i --rm -v /Users/qingxuliang/project/compiler/SysY:/root/compiler maxxing/compiler-dev autotest -riscv -s lv9 /root/compiler
docker run -i --rm -v /Users/qingxuliang/project/compiler/SysY:/root/compiler maxxing/compiler-dev autotest -koopa -s perf /root/compiler
```

如果本地存在额外测试集：

```bash
docker run -i --rm \
  -v /Users/qingxuliang/project/compiler/SysY:/root/compiler \
  -v /tmp/sysy-testsuit-collection/lvX:/opt/bin/testcases/lvX:ro \
  maxxing/compiler-dev \
  autotest -koopa -s lvX /root/compiler
```

baremetal 本地闭环：

```bash
make run-riscv-baremetal \
  INPUT=tests/awesome-sysy/lisp.c \
  INPUT_DATA=tests/fixtures/awesome-sysy/lisp-simple.in
```
