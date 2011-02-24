#ifndef PARSER_H_INCLUDED
#define PARSER_H_INCLUDED

#include "ast.h"
#include <stdio.h>

typedef struct ParseMessage {
    struct ParseMessage *next;      /* link to next message */
    SourceLocation origin;          /* origin of error */
    char *message;                  /* human-readable message */
} ParseMessage;

typedef struct ParseResult {
    AstNode         *ast;           /* resulting abstract syntax tree */
    ParseMessage    *warnings;      /* warnings encountered during parsing */
    ParseMessage    *errors;        /* errors encountered during parsing */
} ParseResult;

/* All of the following functions take an argument indicating the character
   to be recognized as a call to the debug handler (-1 for none).

   The returned result structure must be freed with free_parse_result(). If the
   result contains any errors, the resulting AST may be incomplete. */
extern ParseResult *parse_string(const char *str, int debug);
extern ParseResult *parse_path(const char *path, int debug);
extern ParseResult *parse_file(FILE *fp, int debug);
extern void parse_free_result(ParseResult *result);

#endif /* ndef PARSER_H_INCLUDED */
