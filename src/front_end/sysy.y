%code requires {
#include "ast.h"
}

%{
#include "ast.h"

int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);
extern int yylineno;

using namespace std;
%}

%parse-param { std::unique_ptr<BaseAST> &ast }

// union不可以存在非平凡类型 (std::string, std::unique_ptr)
%union {
    std::string *str_val;
    int int_val;
    UnaryOp unary_op;
    BinaryOp binary_op;
    BaseAST *ast_val;
    std::vector<std::unique_ptr<BaseAST>> *ast_list;
}

// %destructor 是给 parser 错误路径补内存回收
%destructor {
    delete $$;
} <ast_list>

// lexer 返回的所有 token 种类的声明
%token INT RETURN CONST IF ELSE WHILE BREAK CONTINUE VOID
%token <str_val> IDENT
%token ADD SUB BANG MUL DIV MOD EQ NEQ GT LT GE LE AND OR
%token <int_val> INT_CONST

// 非终结符的类型定义
%type <ast_val> CompUnitItem FuncDef Block Stmt Exp PrimaryExp UnaryExp AddExp MulExp LOrExp RelExp EqExp LAndExp
%type <ast_val> Decl ConstDecl ConstDef ConstInitVal BlockItem ConstExp VarDecl VarDef InitVal
%type <ast_val> MatchedStmt UnMatchedStmt FuncFParam FuncRParams LVal DimDecl Index
%type <ast_list> ConstDefList BlockItemList VarDefList CompUnitItemList FuncFParamList ExpList
%type <ast_list> DimDeclList ConstInitValList OptIndexList
%type <int_val> Number
%type <unary_op> UnaryOp
%type <binary_op> AddOp MulOp RelOp EqOp

%%

CompUnit
    : CompUnitItemList {
        auto comp = make_unique<CompUnitAST>();
        comp->items = std::move(*($1));
        delete $1;
        ast = std::move(comp);
    }
    ;

CompUnitItemList
    : CompUnitItem {
        auto item_list = new vector<unique_ptr<BaseAST>>();
        item_list->push_back(unique_ptr<BaseAST>($1));
        $$ = item_list;
    }
    | CompUnitItemList CompUnitItem {
        auto item_list = $1;
        item_list->push_back(unique_ptr<BaseAST>($2));
        $$ = item_list;
    }
    ;

CompUnitItem
    : Decl {
        $$ = $1;
    }
    | FuncDef {
        $$ = $1;
    }
    ;

// 注意, 这里没有将 INT 和 VOID 归为FuncType, 将两者分开可以避免和 VarDecl 开头类型起冲突
FuncDef
    : INT IDENT '(' FuncFParamList ')' Block {
        auto ast = new FuncDefAST();
        ast->func_type = FuncType::INT;
        ast->ident = *unique_ptr<string>($2);
        ast->block = unique_ptr<BaseAST>($6);
        if ($4 != nullptr) {
            ast->func_f_params = std::move(*($4));
            delete $4;
        }
        $$ = ast;
    }
    | VOID IDENT '(' FuncFParamList ')' Block {
        auto ast = new FuncDefAST();
        ast->func_type = FuncType::VOID;
        ast->ident = *unique_ptr<string>($2);
        ast->block = unique_ptr<BaseAST>($6);
        if ($4 != nullptr) {
            ast->func_f_params = std::move(*($4));
            delete $4;
        }
        $$ = ast;
    }
    ;

FuncFParamList
    : %empty {
        $$ = nullptr;
    }
    | FuncFParam {
        auto func_f_param_list = new vector<unique_ptr<BaseAST>>();
        func_f_param_list->push_back(unique_ptr<BaseAST>($1));
        $$ = func_f_param_list;
    }
    | FuncFParamList ',' FuncFParam {
        auto func_f_param_list = $1 == nullptr ? new vector<unique_ptr<BaseAST>> : $1;
        func_f_param_list->push_back(unique_ptr<BaseAST>($3));
        $$ = func_f_param_list;
    }
    ;

FuncFParam
    : INT IDENT {
        auto func_f_param = new FuncFParamAST();
        func_f_param->type = BType::INT;
        func_f_param->ident = *unique_ptr<string>($2);
        func_f_param->payload = FuncFParamAST::Scalar{};
        $$ = func_f_param;
    }
    | INT IDENT '[' ']' {
        auto func_f_param = new FuncFParamAST();
        func_f_param->type = BType::INT;
        func_f_param->ident = *unique_ptr<string>($2);
        func_f_param->payload = FuncFParamAST::Array{ {} };
        $$ = func_f_param;
    }
    | INT IDENT '[' ']' DimDeclList{
        auto func_f_param = new FuncFParamAST();
        func_f_param->type = BType::INT;
        func_f_param->ident = *unique_ptr<string>($2);
        func_f_param->payload = FuncFParamAST::Array {
            std::move(*($5))
        };
        delete $5;
        $$ = func_f_param;
    }
    ;

Decl
    : ConstDecl {
        auto ast = new DeclAST();
        ast->const_or_var = unique_ptr<BaseAST>($1); 
        $$ = ast;
    }
    | VarDecl {
        auto ast = new DeclAST();
        ast->const_or_var = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    ;

ConstDecl
    : CONST INT ConstDefList ';' {
        auto ast = new ConstDeclAST();
        ast->type = BType::INT;
        ast->const_defs = std::move(*($3));
        delete $3;
        $$ = ast;
    }
    ;

ConstDefList
    : ConstDef {
        auto const_def_list = new vector<unique_ptr<BaseAST>>();
        const_def_list->push_back(unique_ptr<BaseAST>($1));
        $$ = const_def_list;
    }
    | ConstDefList ',' ConstDef {
        auto const_def_list = $1;
        const_def_list->push_back(unique_ptr<BaseAST>($3));
        $$ = const_def_list;
    }
    ;

ConstDef
    : IDENT '=' ConstInitVal {
        auto ast = new ConstDefAST();
        ast->payload = ConstDefAST::ConstExpr {
            *unique_ptr<string>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    | IDENT DimDeclList '=' ConstInitVal {
        auto ast = new ConstDefAST();
        ast->payload = ConstDefAST::ConstArray {
            *unique_ptr<string>($1),
            std::move(*($2)),
            unique_ptr<BaseAST>($4)
        };
        delete $2;
        $$ = ast;
    }
    ;

ConstInitVal
    : ConstExp {
        auto ast = new ConstInitValAST();
        ast->payload = ConstInitValAST::ConstExprInit {
            unique_ptr<BaseAST>($1)
        };
        $$ = ast;
    }
    | '{' '}' {
        auto ast = new ConstInitValAST();
        ast->payload = ConstInitValAST::ConstArrayInit { {} };
        $$ = ast;
    }
    | '{' ConstInitValList '}' {
        auto ast = new ConstInitValAST();
        if ($2 != nullptr) {
            ast->payload = ConstInitValAST::ConstArrayInit {
                std::move(*($2))
            };
            delete $2;
        }
        $$ = ast;
    }
    ;

ConstInitValList
    : ConstInitVal {
        auto init_list = new vector<unique_ptr<BaseAST>>();
        init_list->push_back(unique_ptr<BaseAST>($1));
        $$ = init_list;
    }
    | ConstInitValList ',' ConstInitVal {
        auto init_list = $1;
        init_list->push_back(unique_ptr<BaseAST>($3));
        $$ = init_list;
    }
    ;

ConstExp
    : Exp {
        $$ = $1;
    }
    ;

// Multi-dimensional array dimension declaration
DimDeclList
    : DimDecl {
        auto dim_list = new vector<unique_ptr<BaseAST>>();
        dim_list->push_back(unique_ptr<BaseAST>($1));
        $$ = dim_list;
    }
    | DimDeclList DimDecl {
        auto dim_list = $1;
        dim_list->push_back(unique_ptr<BaseAST>($2));
        $$ = dim_list;
    }
    ;

DimDecl
    : '[' ConstExp ']' {
        $$ = $2;
    }
    ;

VarDecl
    : INT VarDefList ';' {
        auto ast = new VarDeclAST();
        ast->type = BType::INT;
        ast->var_defs = std::move(*($2));
        delete $2;
        $$ = ast;
    }
    ;

VarDefList
    : VarDef {
        auto var_def_list = new vector<unique_ptr<BaseAST>>();
        var_def_list->push_back(unique_ptr<BaseAST>($1));
        $$ = var_def_list;
    }
    | VarDefList ',' VarDef {
        auto var_def_list = $1;
        var_def_list->push_back(unique_ptr<BaseAST>($3));
        $$ = var_def_list;
    }
    ;

VarDef
    : IDENT {
        auto ast = new VarDefAST();
        ast->payload = VarDefAST::UninitializedScalar{
            *unique_ptr<string>{$1}
        };
        $$ = ast;
    }
    | IDENT '=' InitVal {
        auto ast = new VarDefAST();
        ast->payload = VarDefAST::InitializedScalar{
            *unique_ptr<string>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    | IDENT DimDeclList {
        auto ast = new VarDefAST();
        auto* dim_list = $2;
        ast->payload = VarDefAST::UninitializedArray {
            *unique_ptr<string>($1),
            std::move(*dim_list)
        };
        delete dim_list;
        $$ = ast;
    }
    | IDENT DimDeclList '=' ConstInitVal {
        auto ast = new VarDefAST();
        auto* dim_list = $2;
        ast->payload = VarDefAST::InitializedArray {
            *unique_ptr<string>($1),
            std::move(*dim_list),
            unique_ptr<BaseAST>($4)
        };
        delete dim_list;
        $$ = ast;
    }
    ;

InitVal
    : Exp {
        $$ = $1;
    }
    ;



Block
    : '{' BlockItemList '}' {
        auto ast = new BlockAST();
        // BlockItemList may be empty
        if ($2 != nullptr) {
            ast->items = std::move(*($2));
            delete $2;
        }
        $$ = ast;
    }
    ;

BlockItemList
    : %empty {
        $$ = nullptr;
    }
    | BlockItemList BlockItem {
        auto block_item_list = $1 == nullptr ? new vector<unique_ptr<BaseAST>>() : $1;
        block_item_list->push_back(unique_ptr<BaseAST>($2)); 
        $$ = block_item_list;
    }
    ;

BlockItem
    : Decl {
        $$ = $1;
    }
    | Stmt {
        $$ = $1;
    }
    ;

Stmt
    : MatchedStmt {
        $$ = $1;
    }
    | UnMatchedStmt {
        $$ = $1;
    }
    ;

MatchedStmt
    : RETURN Exp ';' {
        auto ast = new StmtAST();
        ast->payload = StmtAST::Return { unique_ptr<BaseAST>($2) };
        $$ = ast;
    }
    | RETURN ';' {
        auto ast = new StmtAST();
        ast->payload = StmtAST::Return { nullptr };
        $$ = ast;
    }
    | LVal '=' Exp ';'
    {
        auto ast = new StmtAST();
        ast->payload = StmtAST::Assign {
            unique_ptr<BaseAST>($1),
            unique_ptr<BaseAST>($3) 
        };
        $$ = ast;
    }
    | Exp ';' {
        auto ast = new StmtAST();
        ast->payload = StmtAST::Exp {unique_ptr<BaseAST>($1) };
        $$ = ast;
    } 
    | ';' {
        auto ast = new StmtAST();
        ast->payload = StmtAST::Exp { nullptr };
        $$ = ast;
    }
    | Block {
        auto ast = new StmtAST();
        ast->payload = StmtAST::Block { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | WHILE '(' Exp ')' MatchedStmt {
        auto ast = new StmtAST();
        ast->payload = StmtAST::LoopStmt {
           unique_ptr<BaseAST>($3),
           unique_ptr<BaseAST>($5)
        };
        $$ = ast;
    }
    | BREAK ';' {
        auto ast = new StmtAST();
        ast->payload = StmtAST::LoopControlStmt{ StmtAST::LoopControlStmt::Kind::Break };
        $$ = ast;
    }
    | CONTINUE ';' {
        auto ast = new StmtAST();
        ast->payload = StmtAST::LoopControlStmt{ StmtAST::LoopControlStmt::Kind::Continue };
        $$ = ast;
    }
    |  IF '(' Exp ')' MatchedStmt ELSE MatchedStmt {
        auto ast = new StmtAST();
        ast->payload = StmtAST::SelectStmt {
            unique_ptr<BaseAST>($3),
            unique_ptr<BaseAST>($5),
            unique_ptr<BaseAST>($7)
        };
        $$ = ast; 
    }
    ;

UnMatchedStmt
    : IF '(' Exp ')' Stmt {
        auto ast = new StmtAST();
        ast->payload = StmtAST::SelectStmt {
            unique_ptr<BaseAST>($3),
            unique_ptr<BaseAST>($5),
            nullptr
        };
        $$ = ast;
    }
    | WHILE '(' Exp ')' UnMatchedStmt {
        auto ast = new StmtAST();
        ast->payload = StmtAST::LoopStmt {
           unique_ptr<BaseAST>($3),
           unique_ptr<BaseAST>($5)
        };
        $$ = ast;
    }
    | IF '(' Exp ')' MatchedStmt ELSE UnMatchedStmt {
        auto ast = new StmtAST();
        ast->payload = StmtAST::SelectStmt {
            unique_ptr<BaseAST>($3),
            unique_ptr<BaseAST>($5),
            unique_ptr<BaseAST>($7)
        };
        $$ = ast;
    }
    ;

Exp
    : LOrExp {
        auto ast = new ExpAST();
        ast->lor_exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    ;

LOrExp
    : LAndExp {
        auto ast = new LOrExpAST();
        ast->payload = LOrExpAST::Simple { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | LOrExp OR LAndExp {
        auto ast = new LOrExpAST();
        ast->payload = LOrExpAST::Binary {
            BinaryOp::LOR,
            unique_ptr<BaseAST>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    ;

LAndExp
    : EqExp {
        auto ast = new LAndExpAST();
        ast->payload = LAndExpAST::Simple { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | LAndExp AND EqExp {
        auto ast = new LAndExpAST();
        ast->payload = LAndExpAST::Binary {
            BinaryOp::LAND,
            unique_ptr<BaseAST>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    ;

EqExp
    : RelExp {
        auto ast = new EqExpAST();
        ast->payload = EqExpAST::Simple { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | EqExp EqOp RelExp {
        auto ast = new EqExpAST();
        ast->payload = EqExpAST::Binary {
            $2,
            unique_ptr<BaseAST>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    ;

EqOp
    : EQ {
        $$ = BinaryOp::EQ;
    }
    | NEQ {
        $$ = BinaryOp::NEQ;
    }
    ;

RelExp
    : AddExp {
        auto ast = new RelExpAST();
        ast->payload = RelExpAST::Simple { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | RelExp RelOp AddExp {
        auto ast = new RelExpAST();
        ast->payload = RelExpAST::Binary {
            $2,
            unique_ptr<BaseAST>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    ;

RelOp
    : LT {
        $$ = BinaryOp::LT;
    }
    | GT {
        $$ = BinaryOp::GT;
    }
    | LE {
        $$ = BinaryOp::LE;
    }
    | GE {
        $$ = BinaryOp::GE;
    }
    ;


MulExp
    : UnaryExp {
        auto ast = new MulExpAST();
        ast->payload = MulExpAST::Simple { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | MulExp MulOp UnaryExp {
        auto ast = new MulExpAST();
        ast->payload = MulExpAST::Binary {
            $2,
            unique_ptr<BaseAST>($1),  // mul_exp (lhs)
            unique_ptr<BaseAST>($3)   // unary_exp (rhs)
        };
        $$ = ast;
    }
    ;

AddExp
    : MulExp {
        auto ast = new AddExpAST();
        ast->payload = AddExpAST::Simple { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | AddExp AddOp MulExp {
        auto ast = new AddExpAST();
        ast->payload = AddExpAST::Binary {
            $2,
            unique_ptr<BaseAST>($1),
            unique_ptr<BaseAST>($3)
        };
        $$ = ast;
    }
    ;

MulOp
    : MUL {
        $$ = BinaryOp::MUL;
    }
    | DIV {
        $$ = BinaryOp::DIV;
    }
    | MOD {
        $$ = BinaryOp::MOD;
    }
    ;

AddOp
    : ADD {
        $$ = BinaryOp::ADD;
    }
    | SUB {
        $$ = BinaryOp::SUB;
    }
    ;

// unary operation 一元表达式
UnaryExp
    : PrimaryExp {
        auto ast = new UnaryExpAST();
        ast->payload = UnaryExpAST::Primary { unique_ptr<BaseAST>($1) };
        $$ = ast;
    }
    | UnaryOp UnaryExp {
        auto ast = new UnaryExpAST();
        ast->payload = UnaryExpAST::Unary { $1, unique_ptr<BaseAST>($2) };
        $$ = ast;
    }
    | IDENT '(' ')' {
        auto ast = new UnaryExpAST();
        ast->payload = UnaryExpAST::FuncCall { *unique_ptr<string>($1), nullptr };
        $$ = ast;
    }
    | IDENT '(' FuncRParams ')' {
        auto ast = new UnaryExpAST();
        ast->payload = UnaryExpAST::FuncCall { *unique_ptr<string>($1), unique_ptr<BaseAST>($3)};
        $$ = ast;
    }
    ;

FuncRParams 
    : ExpList {
        auto ast = new FuncRParamsAST();
        ast->exps = std::move(*($1));
        delete $1;
        $$ = ast;
    }
    ;

ExpList
    : Exp {
        auto exps = new vector<unique_ptr<BaseAST>>();
        exps->push_back(unique_ptr<BaseAST>($1));
        $$ = exps;
    }
    | ExpList ',' Exp {
        auto exps = $1;
        exps->push_back(unique_ptr<BaseAST>($3));
        $$ = exps;
    }
    ;


UnaryOp
    : ADD {
        $$ = UnaryOp::PLUS;
    }
    | SUB {
        $$ = UnaryOp::MINUS;
    }
    | BANG {
        $$ = UnaryOp::NOT;
    } 
    ;

PrimaryExp
    : '(' Exp ')' {
        auto ast = new PrimaryExpAST();
        ast->payload = PrimaryExpAST::Expression { 
            unique_ptr<BaseAST>($2) 
        };
        $$ = ast;
    }
    | Number {
        auto ast = new PrimaryExpAST();
        ast->payload = PrimaryExpAST::Number { $1 };
        $$ = ast;
    }
    | LVal {
        auto ast = new PrimaryExpAST();
        ast->payload = PrimaryExpAST::LValue { 
            unique_ptr<BaseAST>($1)
        };
        $$ = ast;
    }
    ;

LVal
    : IDENT OptIndexList {
        auto ast = new LValAST();
        ast->ident = *unique_ptr<string>($1);
        auto* index_list = $2;
        ast->indices = std::move(*index_list);
        delete index_list;
        $$ = ast;
    }
    ;

OptIndexList
    : %empty {
        $$ = new vector<unique_ptr<BaseAST>>();
    }
    | OptIndexList Index {
        auto index_list = $1;
        index_list->push_back(unique_ptr<BaseAST>($2));
        $$ = index_list;
    }
    ;

Index
    : '[' Exp ']' {
        $$ = $2;
    }
    ;

Number
    : INT_CONST {
        $$ = $1;
    }
    ;

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
    cerr << "error at line " << yylineno << ": " << s << endl;
}
