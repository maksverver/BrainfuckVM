/* This file contains sample wrapper code that can be compiled and linked
   against an object file produced by the JIT compiler. The Brainfuck program
   is in a function called bfmain() that takes two arguments: a pointer to the
   start of the blank tape, and an array of function pointers that are used to
   call I/O functions. */

#include <stdio.h>

typedef char Cell;
typedef Cell *(*Callback)(Cell *);

extern Cell *bfmain(Cell *tape, Callback callbacks[3]);

static Cell *read(Cell *head)
{
    int c = getchar();
    if (c != EOF) *head = c;
    return head;
}

static Cell *write(Cell *head)
{
    putchar(*head);
    return head;
}

static Cell *dummy(Cell *head)
{
    return head;
}

int main()
{
    static Callback callbacks[3] = { read, write, dummy, dummy };
    static Cell tape[1<<16] = { 0 };
    bfmain(tape, callbacks);
    return 0;
}
