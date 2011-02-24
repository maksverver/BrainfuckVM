#ifndef AST_H_INCLUDED
#define AST_H_INCLUDED

#include <stdlib.h>
#include <stdio.h>

/* Function callbacks: */
typedef enum {
    CB_READ,
    CB_WRITE,
    CB_DEBUG,
    CB_WRAPPED,
    CB_COUNT
} CallType;

/* A source location packs a line and a column number in an integer: */
typedef unsigned long SourceLocation;
#define SRCLOC(line, column) (1000000UL*(line) + (column))
#define SRCLOC_LINE(srcloc) ((int)(srcloc/1000000UL))
#define SRCLOC_COLUMN(srcloc) ((int)(srcloc%1000000UL))

typedef struct SourceSpan {
    SourceLocation begin, end;  /* closed range of source locations */
} SourceSpan;

typedef struct CodeSpan {
    size_t begin, end;  /* range of code (offsets; end is exclusive) */
} CodeSpan;

typedef enum {
    OP_NONE,
    OP_LOOP,   /* child contains the list of instructions in the loop */
    OP_ADD,    /* `value' contains the difference */
    OP_MOVE,   /* `value' contains the distance */
    OP_CALL,   /* `value' is the function number */
    OP_ADD_MOVE /* `value' is the distance; `add' points to differences */
} OpType;

/* An AST Node structure represents a syntactic element in the parsed Brainfuck
   program. A program is represented as a tree where internal nodes are LOOP
   instructions and leaf nodes are ADD, MOVE and CALL instructions.

   For type == OP_ADD_MOVE, `begin' and `end' describe the range over which the
   tape head moves, and `add' points into the middle of an array that describes
   the values added at indices in range [begin:end). */
typedef struct AstNode {
    struct AstNode *next, *child;
    OpType type;
    int value;
    SourceSpan origin;
    CodeSpan code;
    /* The following fields are used only when type == OP_ADD_MOVE: */
    int begin, end;         /* range over which the head moves */
    signed char *add;       /* value added to each cell in range */
} AstNode;

/* Returns a deep copy of the given AST node (which may be NULL). */
extern AstNode *ast_clone(AstNode *node);

/* Recursively frees the given AST node (may be NULL) and all its children. */
extern void ast_free(AstNode *node);

/* Print the AST (for debugging purposes). */
extern void ast_debug_dump(FILE *fp, const AstNode *root);

#endif /* ndef AST_H_INCLUDED */
