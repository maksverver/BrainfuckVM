#ifndef VM_H
#define VM_H

#include "ast.h"
#include <stdlib.h>
#include <stdio.h>

typedef unsigned char Cell;

typedef Cell *(*VM_Callback)(Cell *tape);

extern VM_Callback vm_callbacks[CB_COUNT];

extern void vm_init(void);
extern void vm_load(AstNode *ast);
extern void vm_limit_mem(size_t size);
extern void vm_exec(void);
extern void vm_fini(void);
extern void vm_dump(FILE *fp);

#endif /* ndef VM_H */
