#ifndef DEBUGGER_H_INCLUDED
#define DEBUGGER_H_INCLUDED

#include "vm.h"
#include "ast.h"

void debug_break(Cell **head, const AstNode *program, size_t offset);

#endif /* ndef DEBUGGER_H_INCLUDED */
