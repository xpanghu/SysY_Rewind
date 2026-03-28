%code requires {
#include "ast.h"
}

%{

#include "ast.h"

// 声明 lexer函数和函数处理函数
int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);
extern int yylineno;

using namespace std;
%}

%parse-param { std::unique_ptr<BaseAST> &ast}

// union不可以存在非平凡类型（std::string）
%union {
    std::string *str_val;
    int int_val;
    BaseAST *ast_val;
}

// lexer 返回的所有 token 种类的声明
// 注意 IDENT 和 INT_CONST 会返回 token 的值, 分别对应 str_val 和 int_val
%token INT RETURN 
%token <str_val> IDENT
%token ADD SUB BANG MUL DIV MOD EQ NEQ GT LT GE LE AND OR
%token <int_val> INT_CONST

// 非终结符的类型定义
%type <ast_val> FuncDef FuncType Block Stmt Exp PrimaryExp UnaryExp AddExp MulExp LOrExp RelExp EqExp LAndExp
%type <int_val> Number
%type <str_val> UnaryOp AddOp MulOp RelOp EqOp
%%

CompUnit
    : FuncDef {
        auto comp_unit = make_unique<CompUnitAST>();
        comp_unit->func_def = unique_ptr<BaseAST>($1);
        ast = std::move(comp_unit); 
    }
    ;

FuncDef
    : FuncType IDENT '(' ')' Block {
        auto ast = new FuncDefAST();
        ast->func_type = unique_ptr<BaseAST>($1);
        ast->ident = *unique_ptr<string>($2);
        ast->block = unique_ptr<BaseAST>($5); 
        $$ = ast; 
    }
    ;

FuncType
    : INT {
        auto ast = new FuncTypeAST();
        ast->type = "int";
        $$ = ast;
    }
    ;

Block
    : '{' Stmt '}' {
        auto ast = new BlockAST();
        ast->stmt = unique_ptr<BaseAST>($2);
        $$ = ast;
    }
    ;

Stmt
    : RETURN Exp ';' {
        auto ast = new StmtAST();
        ast->kind = StmtKind::RETURN;
        ast->return_exp = unique_ptr<BaseAST>($2);
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
        ast->epk = LOrExpKind::LAND;
        ast->land_exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    | LOrExp OR LAndExp {
        auto ast = new LOrExpAST();
        ast->epk = LOrExpKind::LORANDLAND;
        ast->lor_exp = unique_ptr<BaseAST>($1);
        ast->land_exp = unique_ptr<BaseAST>($3);
        ast->op = "||";
        $$ = ast;
    }
    ;

LAndExp
    : EqExp {
        auto ast = new LAndExpAST();
        ast->epk = LAndExpKind::EQ;
        ast->eq_exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    | LAndExp AND EqExp {
        auto ast = new LAndExpAST();
        ast->epk = LAndExpKind::LANDANDEQ;
        ast->land_exp = unique_ptr<BaseAST>($1);
        ast->eq_exp = unique_ptr<BaseAST>($3);
        ast->op = "&&";
        $$ = ast;
    }
    ;

EqExp
    : RelExp {
        auto ast = new EqExpAST();
        ast->epk = EqExpKind::REL;
        ast->rel_exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    | EqExp EqOp RelExp {
        auto ast = new EqExpAST();
        ast->epk = EqExpKind::EQANDREL;
        ast->eq_exp = unique_ptr<BaseAST>($1);
        ast->rel_exp = unique_ptr<BaseAST>($3);
        ast->op = *unique_ptr<string>($2);
        $$ = ast;
    }
    ;

EqOp
    : EQ {
        $$ = new string("==");
    }
    | NEQ {
        $$ = new string("!=");
    }
    ;

RelExp
    : AddExp {
        auto ast = new RelExpAST();
        ast->epk = RelExpKind::ADD;
        ast->add_exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    | RelExp RelOp AddExp {
        auto ast = new RelExpAST();
        ast->epk = RelExpKind::RELANDADD;
        ast->rel_exp = unique_ptr<BaseAST>($1);
        ast->add_exp = unique_ptr<BaseAST>($3);
        ast->op = *unique_ptr<string>($2);
        $$ = ast;
    }
    ;

RelOp
    : LT {
        $$ = new string("<");
    }
    | GT {
        $$ = new string(">");
    }
    | LE {
        $$ = new string("<=");
    }
    | GE {
        $$ = new string(">=");
    }
    ;


MulExp
    : UnaryExp {
        auto ast = new MulExpAST();
        ast->epk = MulExpKind::UNARY;
        ast->unary_exp = unique_ptr<BaseAST>($1);
        $$ = ast; 
    }
    | MulExp MulOp UnaryExp {
        auto ast = new MulExpAST();
        ast->mul_exp = unique_ptr<BaseAST>($1);
        ast->unary_exp = unique_ptr<BaseAST>($3);
        ast->epk = MulExpKind::MULANDUNARY;
        ast->op = *unique_ptr<string>($2);
        $$ = ast;
    }
    ;

AddExp
    : MulExp {
        auto ast = new AddExpAST();
        ast->epk = AddExpKind::MUL;
        ast->mul_exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    | AddExp AddOp MulExp {
        auto ast = new AddExpAST();
        ast->epk = AddExpKind::ADDANDMUL;
        ast->add_exp = unique_ptr<BaseAST>($1);
        ast->mul_exp = unique_ptr<BaseAST>($3);
        ast->op = *unique_ptr<string>($2);
        $$ = ast;
    }
    ;

MulOp
    : MUL {
        $$ = new string("*");
    }
    | DIV {
        $$ = new string("/");
    }
    | MOD {
        $$ = new string("%");
    }
    ;

AddOp
    : ADD {
        $$ = new string("+");
    }
    | SUB {
        $$ = new string("-");
    }
    ;

PrimaryExp
    : '(' Exp ')' {
        auto ast = new PrimaryExpAST();
        ast->epk = PrimaryExpKind::EXP;
        ast->exp = unique_ptr<BaseAST>($2);
        $$ = ast;
    }
    | Number {
        auto ast = new PrimaryExpAST();
        ast->epk = PrimaryExpKind::NUM;
        ast->number = $1;
        $$ = ast;  
    }
    ;

// unary operation 一元表达式
UnaryExp
    : PrimaryExp {
        auto ast = new UnaryExpAST();
        ast->epk = UnaryExpKind::PRIMARY;
        ast->exp = unique_ptr<BaseAST>($1);
        $$ = ast;
    }
    | UnaryOp UnaryExp {
        auto ast = new UnaryExpAST();
        ast->epk = UnaryExpKind::OP;
        ast->op  = *unique_ptr<string>($1); 
        ast->exp = unique_ptr<BaseAST>($2);
        $$ = ast;
    }
    ;

UnaryOp
    : ADD {
        $$ = new string("+");
    }
    | SUB {
        $$ = new string("-");
    }
    | BANG {
        $$ = new string("!");
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