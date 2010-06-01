#ifndef AST_PRINTER_H_INCLUDED
#define AST_PRINTER_H_INCLUDED

#include "ast.h"
#include <stdio.h>

extern void ast_print(AstNode *ast, FILE *fp, int linewidth, int debug);

#endif /* ndef AST_PRINTER_H_INCLUDED */
