/* This file contains sample wrapper code that can be compiled and linked
   against an object file produced by the JIT compiler. The Brainfuck program
   is in a function called bfmain() that takes two arguments: a pointer to the
   start of the blank tape, and a callback function used for I/O. */

#include <stdio.h>

extern char *bfmain(char *tape, char *(*callback)(char *, int));

static char tape[1<<16];

static char *callback(char *head, int request)
{
    switch (request)
    {
    case 0:  /* read */
        {
            int ch = getchar();
            if (ch != EOF) *head = ch;
        } break;

    case 1:  /* write */
        putchar(*head);
        break;
    }
    return head;
}

int main()
{
    bfmain(tape, callback);
    return 0;
}
