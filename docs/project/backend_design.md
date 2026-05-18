# Backend Design

这份文档说明 RISC-V 后端如何消费 Rewind IR 并生成汇编。

## 后端输入输出

```mermaid
flowchart LR
  IR["IRModule<br/>函数 / 基本块 / 指令 / 全局对象"] --> Backend["RISC-V Backend"]
  Backend --> Asm["RISC-V Assembly<br/>.s / .asm"]
  Asm --> Runtime["SysY Runtime<br/>libsysy / baremetal support"]
  Runtime --> Elf["ELF<br/>baremetal executable"]
  Elf --> QEMU["QEMU<br/>运行验证"]
```

后端只消费 IR，不直接读取 AST 或 parser 信息。这样可以保证目标代码生成和前端语法细节解耦。

## 后端生成流程

```mermaid
flowchart LR
  IRModule["IRModule"] --> GlobalEmit["Global Emitter<br/>输出全局数据"]
  IRModule --> FunctionEmit["Function Emitter<br/>遍历函数"]

  FunctionEmit --> Frame["FunctionFrame<br/>预扫描并分配栈帧"]
  Frame --> Slots["Slots<br/>alloc 对象 / 指令结果 / 参数 / ra / outgoing args"]
  FunctionEmit --> InstEmit["Instruction Emitter<br/>逐条指令翻译"]

  InstEmit --> ScratchRegs["临时寄存器<br/>t0 / t1 / t2"]
  InstEmit --> StackOps["栈读写<br/>lw / sw"]
  InstEmit --> Asm["汇编文本"]
  GlobalEmit --> Asm
```

当前后端策略优先保证正确性，采用栈式模型：

- 局部变量和数组通过栈帧对象槽保存。
- 有结果的 IR 指令可以分配 value slot。
- 指令执行时把操作数物化到临时寄存器，再把结果写回栈槽。
- 大 offset 超出 RISC-V imm12 范围时，使用临时寄存器辅助地址计算。

## 栈帧和调用约定

```mermaid
flowchart TB
  Func["IRFunction"] --> Scan["预扫描函数指令"]
  Scan --> Objects["alloc 对象和数组<br/>object slots"]
  Scan --> Values["指令结果<br/>value slots"]
  Scan --> Calls["是否存在 call"]
  Scan --> OutArgs["最大调用参数数量"]

  Calls --> RA["需要时保存 ra"]
  OutArgs --> StackArgs["超过 a0-a7 的参数<br/>outgoing arg area"]
  Objects --> FrameSize["计算 frame_size"]
  Values --> FrameSize
  RA --> FrameSize
  StackArgs --> FrameSize
```

函数调用约定：

```mermaid
flowchart TB
  Call["IRCallInst<br/>函数调用"] --> Args{"参数数量"}
  Args --> RegArgs["前 8 个参数<br/>a0-a7"]
  Args --> StackArgs["超过 8 个参数<br/>写入 outgoing arg area"]
  Call --> SaveRA{"当前函数是否包含 call"}
  SaveRA --> RA["在当前函数栈帧保存 ra"]
  Call --> Ret["返回值<br/>a0"]
  Ret --> ResultSlot["非 void call<br/>结果写回栈槽"]
```

## 指令翻译示例

```mermaid
flowchart TB
  Load["IRLoadInst"] --> MatPtr["materialize_pointer<br/>得到地址"]
  MatPtr --> LW["lw<br/>读取值"]

  Store["IRStoreInst"] --> MatValue["materialize_value<br/>得到待写值"]
  Store --> MatDest["materialize_pointer<br/>得到目标地址"]
  MatValue --> SW["sw<br/>写入内存"]
  MatDest --> SW

  GetElemPtr["IRGetElemPtrInst"] --> Base["数组基址"]
  GetElemPtr --> Index["下标值"]
  Base --> Addr["base + index * elem_size"]
  Index --> Addr
```

面试时可以这样总结：

> 当前后端是稳定优先的栈式 RISC-V 后端。它先预扫描 IRFunction 计算栈帧，再逐条翻译 IR 指令。函数调用遵守 a0-a7 参数寄存器和栈上传参规则，必要时保存 ra。这个设计虽然还不是高性能寄存器分配，但正确性强，也方便后续引入虚拟寄存器、活跃区间和 linear scan。
