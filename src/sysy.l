%option noyywrap
%option nounput
%option noinput
%option yylineno

%{

#include <cstdlib>
#include <string>

#include "sysy.tab.hpp"

using namespace std;

%}

/* 空白符和注释 */
WhiteSpace    [ \t\n\r]+
LineComment   "//"[^\n]*
BlockComment  "/*"([^*]|\*+[^*/])*\*+"/"

/* 标识符 */
Identifier    [a-zA-Z_][a-zA-Z0-9_]*

/* 整数字面量 */
Decimal       [1-9][0-9]*
Octal         0[0-7]*
Hexadecimal   0[xX][0-9a-fA-F]+

%%

{WhiteSpace}    { /* 忽略 */ }
{LineComment}   { /* 忽略 */ }
{BlockComment}  { /* 忽略 */ }

"int"           { return INT; }
"return"        { return RETURN; }
"+"             { return ADD; }
"-"             { return SUB; }
"!"             { return BANG; }
"*"             { return MUL; }
"/"             { return DIV; }
"%"             { return MOD; }
"<"             { return LT; }
">"             { return GT; }
"<="            { return LE; }
">="            { return GE; }
"=="            { return EQ; }
"!="            { return NEQ; }
"&&"            { return AND; }
"||"            { return OR; }

{Identifier}    { yylval.str_val = new string(yytext); return IDENT; }

{Decimal}       { yylval.int_val = strtol(yytext, nullptr, 0); return INT_CONST; }
{Octal}         { yylval.int_val = strtol(yytext, nullptr, 0); return INT_CONST; }
{Hexadecimal}   { yylval.int_val = strtol(yytext, nullptr, 0); return INT_CONST; }

.               { return yytext[0]; }

%%
