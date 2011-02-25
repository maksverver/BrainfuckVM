#ifndef VM_H
#define VM_H

#include "ast.h"
#include <stdlib.h>
#include <stdio.h>

typedef unsigned char Cell;

typedef Cell *(*VM_Callback)(Cell *tape, int operation);

extern void vm_init(void);
extern void vm_load(AstNode *ast);
extern void vm_set_input(FILE *fp);
extern void vm_set_output(FILE *fp);
extern void vm_set_memlimit(size_t size);
extern void vm_set_eof_value(int val);
extern void vm_set_wrap_check(int enable);
extern void vm_exec(void);
extern void vm_fini(void);
extern void vm_dump(FILE *fp);

/* For the debugger interface: */
extern void vm_expand(Cell **head);
extern Cell *vm_memory(size_t *size);

#endif /* ndef VM_H */
