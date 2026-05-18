# Frontend Design

这份文档从词法分析、语法分析、语义边界和 AST 设计四个角度说明前端。

## 前端主流程

```mermaid
flowchart LR
  Source["SysY 源码<br/>input.sysy"] --> Lexer["Lexer<br/>src/front_end/sysy.l"]
  Lexer --> Tokens["Token Stream<br/>关键字 / 标识符 / 整数 / 运算符"]
  Tokens --> Parser["Parser<br/>src/front_end/sysy.y"]
  Parser --> AST["AST<br/>include/front_end/ast.h"]
  AST --> Dump["AST Dump<br/>src/front_end/ast.cpp"]
  AST --> Lowering["Lowering<br/>AST -> Rewind IR"]
```

## 词法分析

```mermaid
flowchart TB
  Lexer["sysy.l"] --> Ignore["忽略空白和注释<br/>WhiteSpace / LineComment / BlockComment"]
  Lexer --> Keywords["识别关键字<br/>int / void / const / if / else / while / return / break / continue"]
  Lexer --> Operators["识别运算符<br/>+ - * / % ! == != <= >= && ||"]
  Lexer --> Ident["识别标识符<br/>[a-zA-Z_][a-zA-Z0-9_]*"]
  Lexer --> IntConst["识别整数<br/>decimal / octal / hex"]

  Keywords --> Tokens["Bison token"]
  Operators --> Tokens
  Ident --> Tokens
  IntConst --> Tokens
```

词法层只做 token 切分，不判断变量是否定义、类型是否匹配，也不处理数组维度是否合法。这些语义信息交给后续 parser action 和 lowering 阶段处理。

## 语法分析

```mermaid
flowchart TB
  Parser["sysy.y"] --> CompUnit["CompUnit<br/>Decl / FuncDef 列表"]
  Parser --> Decl["Decl<br/>ConstDecl / VarDecl"]
  Parser --> FuncDef["FuncDef<br/>int / void 函数"]
  Parser --> Stmt["Stmt<br/>return / assign / block / if / while / break / continue"]
  Parser --> Exp["Exp<br/>按优先级分层"]
  Parser --> Array["Array Syntax<br/>DimDecl / InitVal / LVal indices"]

  Exp --> LOr["LOrExp"]
  LOr --> LAnd["LAndExp"]
  LAnd --> Eq["EqExp"]
  Eq --> Rel["RelExp"]
  Rel --> Add["AddExp"]
  Add --> Mul["MulExp"]
  Mul --> Unary["UnaryExp"]
  Unary --> Primary["PrimaryExp"]
```

语法层有几个设计点：

- `CompUnit` 允许全局声明和函数定义混合出现。
- `FuncDef` 中把 `int` 和 `void` 函数分开写，避免和变量声明开头产生冲突。
- 表达式按优先级拆成 `LOrExp -> LAndExp -> EqExp -> RelExp -> AddExp -> MulExp -> UnaryExp -> PrimaryExp`。
- `MatchedStmt` 和 `UnMatchedStmt` 用来处理 dangling else。
- 多维数组通过 `DimDeclList`、`InitValList`、`ConstInitValList` 和 `LVal` 的 index 列表表达。

## 语义分析边界

```mermaid
flowchart LR
  Parser["Parser Action<br/>构造 AST"] --> AST["AST<br/>保留源程序结构"]
  AST --> Lowering["Lowering<br/>语义检查和 IR 生成"]

  Lowering --> SymbolTable["SymbolTable<br/>作用域 / 变量 / 常量 / 函数"]
  Lowering --> ConstEval["ConstEvaluator<br/>ConstExp 求值"]
  Lowering --> TypeCheck["Type Check<br/>函数参数 / 返回值 / 数组和指针"]
  Lowering --> ArrayInit["Array Init<br/>数组初始化展开"]
```

当前项目里，前端 parser 主要负责构造 AST，真正的语义检查大多发生在 AST 到 IR 的 lowering 阶段。例如：

- 标识符是否定义，由符号表查询处理。
- `ConstExp` 是否能编译期求值，由 `ConstEvaluator` 处理。
- 函数调用实参与形参是否匹配，在 lowering call 时检查。
- 数组维度、数组初始化、数组形参 decay，在 lowering 阶段显式转成 IR 类型和寻址指令。

这个边界的好处是 parser 不需要理解 IR，也不需要直接处理目标相关细节。

## AST 设计

```mermaid
flowchart TB
  BaseAST["BaseAST<br/>虚基类 / Dump 接口"] --> CompUnit["CompUnitAST<br/>全局 Decl / FuncDef 列表"]
  BaseAST --> Decl["DeclAST<br/>ConstDecl / VarDecl"]
  BaseAST --> FuncDef["FuncDefAST<br/>函数类型 / 名称 / 参数 / Block"]
  BaseAST --> Stmt["StmtAST<br/>Return / Assign / Block / If / While / Break / Continue"]
  BaseAST --> Exp["ExpAST<br/>表达式入口"]
  BaseAST --> LVal["LValAST<br/>ident + indices"]

  Exp --> BinaryLevels["LOr / LAnd / Eq / Rel / Add / Mul<br/>按优先级分层"]
  Exp --> Unary["UnaryExpAST<br/>Primary / Unary / FuncCall"]
  Exp --> Primary["PrimaryExpAST<br/>Number / Expression / LValue"]

  Decl --> ConstDef["ConstDefAST<br/>ConstExpr / ConstArray"]
  Decl --> VarDef["VarDefAST<br/>Scalar / Array / Init / Uninit"]
```

AST 节点整体采用：

- `BaseAST` 作为统一基类。
- `std::unique_ptr<BaseAST>` 表达树形所有权。
- `std::vector<std::unique_ptr<BaseAST>>` 表达列表结构。
- `std::variant` 表达同一语法节点的不同形态，比如 `StmtAST::Return`、`StmtAST::Assign`、`StmtAST::SelectStmt`。

面试时可以这样总结：

> 前端只负责把源代码稳定地表达成 AST。词法层切 token，语法层按 SysY 文法构造 AST，AST 用 `unique_ptr` 表达所有权、用 `variant` 表达节点变体。符号、类型、数组初始化和函数调用检查不塞进 parser，而是在 lowering 阶段统一处理。
