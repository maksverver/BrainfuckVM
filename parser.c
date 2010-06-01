#include "parser.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

typedef struct ParseState {
    FILE            *fp;        /* open file to read from if not NULL */
    const char      *buf;       /* string to read from if not NULL */
    int             debug;      /* debug character */
    int             line;       /* current 0-based line */
    int             column;     /* current 0-based column */
    int             depth;      /* current nesting level */
    ParseMessage    **warnings; /* points to next warning message */
    ParseMessage    **errors;   /* points to next error message */
} ParseState;

static void append_message(
    ParseMessage ***end, int line, int column, const char *message )
{
    ParseMessage *msg = malloc(sizeof(ParseMessage));
    assert(msg != NULL);
    msg->next    = NULL;
    msg->line    = line;
    msg->column  = column;
    msg->message = strdup(message);
    assert(msg->message != NULL);
    **end = msg;
    *end = &msg->next;
}

static void emit(struct AstNode *temp, struct AstNode ***end, int new_type)
{
    struct AstNode *copy;

    if (temp->type != OP_NONE)
    {
        copy = ast_clone(temp);
        **end = copy;
        *end = &copy->next;
    }
    temp->next  = NULL;
    temp->child = NULL;
    temp->type  = new_type;
    temp->value = 0;
}

/* Parses characters from `fp' until EOF or a closing ] instruction, or, if
   fp is NULL, takes characters from the string pointed to by `buf' instead.

   The parser is intentionally simple and fast and preserves all Brainfuck
   operations in the original source code, so the exact code can be printed
   back verbatim (but without non-Brainfuck characters) and any errors can be
   detected (without e.g. optimization removing tape-head-out-of-bounds errors).

   Optimization can be done in a separate pass if desired.
*/
static AstNode *parse(ParseState *ps)
{
    struct AstNode *begin = NULL, **end = &begin;
    struct AstNode node = { NULL, NULL, OP_NONE, 0 };
    int c;

    for (;;)
    {
        if (ps->fp != NULL) c = getc(ps->fp);
        else if (ps->buf != NULL && *ps->buf != '\0') c = *ps->buf++;
        else c = EOF;

        ++ps->column;

        switch (c)
        {
        case '[':
            emit(&node, &end, OP_LOOP);
            ++ps->depth;
            node.child = parse(ps);
            --ps->depth;
            break;

        case ']':
            if (ps->depth == 0) {
                append_message(&ps->warnings, ps->line + 1, ps->column,
                               "ignored unmatched closing bracket");
                break;
            }
            emit(&node, &end, OP_NONE);
            return begin;

        case '+':
            if (node.type != OP_ADD || node.value < 0) {
                emit(&node, &end, OP_ADD);
            }
            ++node.value;
            break;

        case '-':
            if (node.type != OP_ADD || node.value > 0) {
                emit(&node, &end, OP_ADD);
            }
            --node.value;
            break;

        case '>':
            if (node.type != OP_MOVE || node.value < 0) {
                emit(&node, &end, OP_MOVE);
            }
            ++node.value;
            break;

        case '<':
            if (node.type != OP_MOVE || node.value > 0) {
                emit(&node, &end, OP_MOVE);
            }
            --node.value;
            break;

        case ',':
            emit(&node, &end, OP_CALL);
            node.value = CB_READ;
            break;

        case '.':
            emit(&node, &end, OP_CALL);
            node.value = CB_WRITE;
            break;

        case EOF:
            --ps->column;
            if (ps->depth != 0) {
                append_message(&ps->warnings, ps->line + 1, ps->column,
                               "closed unmatched opening bracket");
            }
            emit(&node, &end, OP_NONE);
            return begin;

        case '\n':
            ++ps->line;
            ps->column = 0;
            continue;
        }

        if (c == ps->debug) {
            emit(&node, &end, OP_CALL);
            node.value = CB_DEBUG;
        }
    }
}

static ParseResult *make_parse_result(
    AstNode *ast, ParseMessage *warnings, ParseMessage *errors )
{
    ParseResult *result = malloc(sizeof(ParseResult));
    assert(result != NULL);
    result->ast      = ast;
    result->warnings = warnings;
    result->errors   = errors;
    return result;
}

ParseResult *parse_string(const char *str, int debug)
{
    ParseMessage *warnings = NULL, *errors = NULL;
    ParseState state = { NULL, str, debug, 0, 0, 0, &warnings, &errors };
    AstNode *ast = parse(&state);
    return make_parse_result(ast, warnings, errors);
}

ParseResult *parse_path(const char *path, int debug)
{
    ParseMessage *warnings = NULL, *errors = NULL;
    ParseState state = { NULL, NULL, debug, 0, 0, 0, &warnings, &errors };
    AstNode *ast;

    /* Try to open the specified file: */
    if ((state.fp = fopen(path, "rt")) == NULL)
    {
        append_message(&state.errors, 0, 0, "failed to open input file");
        ast = NULL;
    }
    else
    {
        ast = parse(&state);
        fclose(state.fp);
    }

    return make_parse_result(ast, warnings, errors);
}

ParseResult *parse_file(FILE *fp, int debug)
{
    ParseMessage *warnings = NULL, *errors = NULL;
    ParseState state = { fp, NULL, debug, 0, 0, 0, &warnings, &errors };
    AstNode *ast = parse(&state);
    return make_parse_result(ast, warnings, errors);
}

static void free_messages(ParseMessage *message)
{
    while (message != NULL) {
        ParseMessage *next = message->next;
        free(message->message);
        free(message);
        message = next;
    }
}

void parse_free_result(ParseResult *result)
{
    if (result == NULL) return;
    ast_free(result->ast);
    free_messages(result->warnings);
    free_messages(result->errors);
}
