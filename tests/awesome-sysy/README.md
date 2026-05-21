# awesome-sysy 程序使用指南

本目录包含从 [pku-minic/awesome-sysy](https://github.com/pku-minic/awesome-sysy) 仓库拷贝的 SysY 示例程序。

当前项目已经把 bare-metal RISC-V 的汇编、链接和运行流程迁移到 `Makefile`。因此推荐流程是：

1. 使用 `make riscv-elf` 生成 RISC-V 汇编和 ELF 可执行文件。
2. 使用 `make run-riscv-baremetal` 直接启动 QEMU，或手动执行 `qemu-system-riscv32`。
3. 如果程序输出需要保存成纯文件（例如 PPM 图片），先生成 ELF，再手动执行 QEMU 并重定向 stdout。

> 注意：QEMU 命令名是 `qemu-system-riscv32`。如果本机命令名或路径不同，可以通过 `QEMU=/path/to/qemu-system-riscv32` 覆盖。

---

## 快速开始

生成某个程序的 RISC-V 汇编和 ELF：

```bash
make riscv-elf INPUT=tests/awesome-sysy/<程序>.c
```

生成结果位于项目根目录下：

```text
riscv32-baremetal/<程序>/<程序>.s
riscv32-baremetal/<程序>/<程序>.elf
```

直接用 Makefile 启动 QEMU：

```bash
make run-riscv-baremetal INPUT=tests/awesome-sysy/<程序>.c
```

如果程序需要 stdin，稳定输入样例保存在 `tests/fixtures/awesome-sysy/`，可以直接传给 `INPUT_DATA`：

```bash
make run-riscv-baremetal \
  INPUT=tests/awesome-sysy/maze.c \
  INPUT_DATA=tests/fixtures/awesome-sysy/maze.in
```

手动启动 QEMU 的完整形式：

```bash
qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/<程序>/<程序>.elf
```

如果需要输入和输出重定向：

```bash
qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/<程序>/<程序>.elf \
  < tmp/input.txt > tmp/output.txt
```

当前流程不再内置 timeout。像 `sdf.c` 这类无限循环程序需要手动用 `Ctrl-C` 结束 QEMU。

---

## 程序列表

### 1. maze.c - 迷宫生成器

**功能**：使用 DFS 算法生成随机迷宫，输出 PPM 格式图像。

**输入**：两个整数 `seed zoom`

- `seed`：随机种子
- `zoom`：图像缩放级别（推荐 2 或 4）

**终端运行并保存 PPM 图片**：

```bash
mkdir -p tmp

make riscv-elf INPUT=tests/awesome-sysy/maze.c

qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/maze/maze.elf \
  < tests/fixtures/awesome-sysy/maze.in > tmp/maze.ppm
```

**运行时间**：约 **8-10 秒**（zoom=4，输出 804x804 像素）。

**兼容性**：编译通过，运行正确。

**查看图像：**

```bash
magick tmp/maze.ppm tmp/maze.png
open tmp/maze.png
```

---

### 2. mandelbrot.c - 曼德勃罗集绘制

**功能**：绘制曼德勃罗集分形图像，输出 PPM 格式。

**输入**：6 个整数（IEEE-754 单精度浮点数的整数表示）

- `xmin xmax ymin ymax maxiter xres`

**预期用法**：

```bash
mkdir -p tmp
printf "1049275610 1049280643 999820068 1000185140 500 1024\n" > tmp/mandelbrot.in

make riscv-elf INPUT=tests/awesome-sysy/mandelbrot.c

qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/mandelbrot/mandelbrot.elf \
  < tmp/mandelbrot.in > tmp/mandelbrot.ppm
```

**运行时间**：预计数分钟（大量像素输出）。

**兼容性**：当前编译器仍可能编译失败。`mandelbrot.c` 使用了函数声明语法：

```c
int fp_add(int x, int y);
```

当前还需要：

1. 扩展编译器文法支持 `FuncDecl`。
2. 支持把 `mandelbrot.c` 与 `fp-math.c` 一起编译或链接。

---

### 3. lisp.c - LISP 解释器

**功能**：带引用计数垃圾回收的 LISP 解释器，纯 SysY 实现。

**输入**：LISP 程序（从 stdin 读取）

上游 `lisp/README.md` 的使用方式是把完整 Lisp 程序放进输入文件，然后运行解释器读取整个文件。当前 bare-metal runtime 约定输入文件末尾会追加 Ctrl-D/EOT（ASCII 4），`getch()` 收到它后返回 `-1`，这样 `lisp.c` 才能判断 EOF 并退出。

**简单表达式**：

```bash
make run-riscv-baremetal \
  INPUT=tests/awesome-sysy/lisp.c \
  INPUT_DATA=tests/fixtures/awesome-sysy/lisp-simple.in
```

输出：

```text
3
```

**手动 QEMU 运行**：

```bash
make riscv-elf INPUT=tests/awesome-sysy/lisp.c

{ cat tests/fixtures/awesome-sysy/lisp-simple.in; printf '\004'; } | qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/lisp/lisp.elf
```

**上游 Fibonacci 示例**：

```lisp
(define null?
  (lambda (l) (eq? l '())))

(define map
  (lambda (f l)
    (cond ((null? l) '())
          ('t (cons (f (car l)) (map f (cdr l)))))))

(define fib
  (lambda (n)
    (cond ((< n 2) 1)
          ('t (+ (fib (- n 1)) (fib (- n 2)))))))

(define range
  (lambda (a b)
    (cond ((< a b) (cons a (range (+ a 1) b)))
          ('t '()))))

(map fib (range 0 20))
```

**运行时间**：取决于输入程序的复杂度。

**兼容性**：编译通过，运行正确。已验证简单表达式和上游 Fibonacci 示例。

---

### 4. 2048.c - 2048 游戏

**功能**：经典 2048 游戏的 SysY 实现。

**输入**：随机种子 + 交互式操作（`w/a/s/d/q`）。

**用法**：

```bash
make riscv-elf INPUT=tests/awesome-sysy/2048.c

qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/2048/2048.elf
```

启动后通过 UART 串口交互输入：`h` 帮助，`w/s/a/d` 移动，`q` 退出。

**兼容性**：当前可能无法完整运行。若代码依赖标准 C 库接口，需要改成 SysY runtime 支持的 `putint/putch/getint/getch` 等接口。

---

### 5. diophantine.c - 丢番图方程求解器

**功能**：求解形如 `a^x + b = c^y` 的丢番图方程，寻找正整数解 `(x, y)`。

**输入**：模式选择 + 参数

- `0`：显示帮助
- `1`：交互式 shell（输入 a, b, c）
- `2`：静默批量搜索模式（输入范围参数）
- `3`：输出 Lean4 形式化代码

**查看帮助**：

```bash
mkdir -p tmp
printf "0\n" > tmp/diophantine-help.in
make run-riscv-baremetal INPUT=tests/awesome-sysy/diophantine.c INPUT_DATA=tmp/diophantine-help.in
```

**交互式运行**：

```bash
make run-riscv-baremetal INPUT=tests/awesome-sysy/diophantine.c
```

**静默搜索模式**：

```bash
mkdir -p tmp
printf "2 0 2 2 1 1 2 2\n" > tmp/diophantine-search.in

make riscv-elf INPUT=tests/awesome-sysy/diophantine.c

qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/diophantine/diophantine.elf \
  < tmp/diophantine-search.in
```

这里的输入格式是：

```text
2 exclude_trivial a_max a_min b_max b_min c_max c_min
```

上面的示例只搜索 `a=2, b=1, c=2`，适合作为快速可用性测试。如果要测试更大范围，可以改成类似：

```bash
printf "2 0 30 2 30 1 30 2\n" > tmp/diophantine-search.in
```

**运行时间**：

- Help 模式：约 1 秒
- 交互式求解：取决于参数大小，小参数秒级，大参数可能数分钟

**兼容性**：编译通过，运行正确。

---

### 6. sdf.c - 旋转甜甜圈（终端动画）

**功能**：在终端中渲染旋转的 3D 甜甜圈，使用 SDF（有符号距离场）和定点数数学。

**输入**：无。

**用法**：

```bash
make riscv-elf INPUT=tests/awesome-sysy/sdf.c

qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/sdf/sdf.elf
```

**运行时间**：无限循环。`loop_forever()` 中 `while (1)` 永不退出。

**兼容性**：编译通过，运行正确，会不断输出动画帧。

**注意事项**：

- 当前没有自动 timeout，需要手动按 `Ctrl-C` 结束 QEMU。
- 输出包含 ANSI 清屏码（`\033[2J`），在终端中可直接查看动画效果。

---

### 7. fp-math.c - 定点数数学库

**功能**：为 `mandelbrot.c` 提供定点数运算支持（加、减、乘、除、幂、平方根、三角函数）。

**用法**：不单独运行，需要与 `mandelbrot.c` 一起编译或链接使用。

**兼容性**：当前 Makefile 的单输入文件流程还不支持直接把 `mandelbrot.c` 和 `fp-math.c` 作为一组程序构建。

---

## 兼容性总览

| 程序          | 编译 | 运行 | 主要问题                         |
| ------------- | ---- | ---- | -------------------------------- |
| maze.c        | 是   | 是   | 运行较慢（大量 UART 输出）       |
| mandelbrot.c  | 否   | -    | 需要 `FuncDecl` 和多文件链接支持 |
| lisp.c        | 是   | 是   | 输入文件末尾需要 EOF/EOT         |
| 2048.c        | 待定 | 待定 | 可能依赖 libc 或交互输入行为     |
| diophantine.c | 是   | 是   | 无                               |
| sdf.c         | 是   | 是   | 无限循环，需要手动 `Ctrl-C`      |
| fp-math.c     | 是   | -    | 库文件，依赖 mandelbrot.c        |

---

## Makefile 与 QEMU 运行模式

`make riscv-elf` 会完成三步：

1. 使用项目编译器生成 RISC-V 汇编：`riscv32-baremetal/<程序>/<程序>.s`
2. 使用 `riscv64-unknown-elf-gcc` 编译 bare-metal runtime。
3. 链接得到 RISC-V ELF：`riscv32-baremetal/<程序>/<程序>.elf`

`make run-riscv-baremetal` 会在生成 ELF 后自动执行：

```bash
qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/<程序>/<程序>.elf
```

如果只想启动程序并在终端查看输出，用 `make run-riscv-baremetal` 最方便。

如果指定了 `INPUT_DATA`，Makefile 会把输入文件内容送入 QEMU，并在末尾追加 Ctrl-D/EOT（ASCII 4）。bare-metal runtime 会把该字符转换为 `getch() == -1`，用于模拟文件 EOF。`lisp.c` 这类按 EOF 结束读取的程序依赖这个行为。

如果需要把程序 stdout 保存成图片或文本文件，推荐手动执行 QEMU，因为 `make` 会打印构建命令，可能污染输出文件：

```bash
make riscv-elf INPUT=tests/awesome-sysy/maze.c

qemu-system-riscv32 \
  -M virt -bios none -m 128M \
  -display none -monitor none -serial stdio \
  -kernel riscv32-baremetal/maze/maze.elf \
  < tests/fixtures/awesome-sysy/maze.in > tmp/maze.ppm
```

不要这样生成图片：

```bash
make run-riscv-baremetal \
  INPUT=tests/awesome-sysy/maze.c \
  INPUT_DATA=tests/fixtures/awesome-sysy/maze.in > tmp/maze.ppm
```

这会把 `make` 打印的命令文本一起写入 `tmp/maze.ppm`，导致 PPM 图片文件被污染。

清理 RISC-V 产物：

```bash
make clean-riscv
```

清理完整构建产物：

```bash
make clean
```

---

## 输出目录

构建产物保存在项目根目录的 `riscv32-baremetal/`：

```text
riscv32-baremetal/maze/maze.s
riscv32-baremetal/maze/maze.elf
```

稳定输入保存在 `tests/fixtures/`，临时输出建议放在项目本地 `tmp/`：

```bash
mkdir -p tmp
qemu-system-riscv32 ... -kernel riscv32-baremetal/maze/maze.elf \
  < tests/fixtures/awesome-sysy/maze.in > tmp/maze.ppm
```

---

## 当前已验证命令

以下命令已经在当前项目环境中验证通过：

```bash
mkdir -p tmp
make riscv-elf INPUT=tests/awesome-sysy/maze.c
qemu-system-riscv32 -M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel riscv32-baremetal/maze/maze.elf < tests/fixtures/awesome-sysy/maze.in > tmp/maze.ppm
magick tmp/maze.ppm tmp/maze.png
```

验证结果：`tmp/maze.ppm` 是 `804x804` 的合法 PPM，`tmp/maze.png` 可以正常生成。

```bash
printf "0\n" > tmp/diophantine-help.in
make riscv-elf INPUT=tests/awesome-sysy/diophantine.c
qemu-system-riscv32 -M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel riscv32-baremetal/diophantine/diophantine.elf < tmp/diophantine-help.in > tmp/diophantine-help.out
```

验证结果：`tmp/diophantine-help.out` 正常输出 help 文本。

```bash
printf "2 0 2 2 1 1 2 2\n" > tmp/diophantine-search.in
qemu-system-riscv32 -M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel riscv32-baremetal/diophantine/diophantine.elf < tmp/diophantine-search.in > tmp/diophantine-search.out
```

验证结果：`tmp/diophantine-search.out` 正常输出 Lean4 形式化代码片段。

```bash
make run-riscv-baremetal INPUT=tests/awesome-sysy/lisp.c INPUT_DATA=tests/fixtures/awesome-sysy/lisp-simple.in
```

验证结果：`lisp.c` 正常输出 `3` 并退出。

```bash
make riscv-elf INPUT=tests/awesome-sysy/lisp.c
{ cat tests/fixtures/awesome-sysy/lisp-fib.in; printf '\004'; } | qemu-system-riscv32 -M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel riscv32-baremetal/lisp/lisp.elf > tmp/lisp-fib.out
```

验证结果：`tmp/lisp-fib.out` 正常输出：

```text
(1 1 2 3 5 8 13 21 34 55 89 144 233 377 610 987 1597 2584 4181 6765)
```

```bash
make riscv-elf INPUT=tests/awesome-sysy/sdf.c
qemu-system-riscv32 -M virt -bios none -m 128M -display none -monitor none -serial stdio -kernel riscv32-baremetal/sdf/sdf.elf
```

验证结果：`sdf.c` 会持续输出终端动画帧；这是无限循环程序，测试时需要手动按 `Ctrl-C` 结束。
