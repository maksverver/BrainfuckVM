#include "parser.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ParseState {
    FILE            *fp;        /* open file to read from if not NULL */
    const char      *buf;       /* string to read from if not NULL */
    int             debug;      /* debug character */
    int             separator;  /* input separator */
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
    msg->next = NULL;
    msg->origin = SRCLOC(line, column);
    msg->message = strdup(message);
    assert(msg->message != NULL);
    **end = msg;
    *end = &msg->next;
}

static void emit(struct AstNode *temp, struct AstNode ***end,
                 int new_type, const ParseState *ps)
{
    struct AstNode *copy;

    if (temp->type != OP_NONE)
    {
        size_t size = offsetof(AstNode, begin);
        copy = malloc(size);
        memcpy(copy, temp, size);
        **end = copy;
        *end = &copy->next;
    }
    memset(temp, 0, sizeof(*temp));
    temp->type = new_type;
    temp->origin.begin = SRCLOC(ps->line + 1, ps->column);
    temp->origin.end = temp->origin.begin;
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
    struct AstNode node;
    int c;

    memset(&node, 0, sizeof(node));
    for (;;)
    {
        if (ps->fp != NULL) {
            c = getc(ps->fp);
            if (c == ps->separator) {
                ungetc(c, ps->fp);
                c = EOF;
            }
        } else if (ps->buf != NULL && *ps->buf != '\0') {
            c = *ps->buf++;
        } else {
            c = EOF;
        }

        ++ps->column;

        switch (c)
        {
        case '[':
            emit(&node, &end, OP_LOOP, ps);
            ++ps->depth;
            node.child = parse(ps);
            --ps->depth;
            node.origin.end = SRCLOC(ps->line + 1, ps->column);
            break;

        case ']':
            if (ps->depth == 0) {
                append_message(&ps->warnings, ps->line + 1, ps->column,
                               "ignored unmatched closing bracket");
                break;
            }
            emit(&node, &end, OP_NONE, ps);
            return begin;

        case '+':
            if (node.type != OP_ADD || node.value < 0) {
                emit(&node, &end, OP_ADD, ps);
            } else {
                node.origin.end = SRCLOC(ps->line + 1, ps->column);
            }
            ++node.value;
            break;

        case '-':
            if (node.type != OP_ADD || node.value > 0) {
                emit(&node, &end, OP_ADD, ps);
            } else {
                node.origin.end = SRCLOC(ps->line + 1, ps->column);
            }
            --node.value;
            break;

        case '>':
            if (node.type != OP_MOVE || node.value < 0) {
                emit(&node, &end, OP_MOVE, ps);
            } else {
                node.origin.end = SRCLOC(ps->line + 1, ps->column);
            }
            ++node.value;
            break;

        case '<':
            if (node.type != OP_MOVE || node.value > 0) {
                emit(&node, &end, OP_MOVE, ps);
            } else {
                node.origin.end = SRCLOC(ps->line + 1, ps->column);
            }
            --node.value;
            break;

        case ',':
            emit(&node, &end, OP_CALL, ps);
            node.value = CB_READ;
            break;

        case '.':
            emit(&node, &end, OP_CALL, ps);
            node.value = CB_WRITE;
            break;

        case EOF:
            --ps->column;
            if (ps->depth != 0) {
                append_message(&ps->warnings, ps->line + 1, ps->column,
                               "closed unmatched opening bracket");
            }
            emit(&node, &end, OP_NONE, ps);
            return begin;

        case '\n':
            ++ps->line;
            ps->column = 0;
            continue;
        }

        if (c == ps->debug) {
            emit(&node, &end, OP_CALL, ps);
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
    ParseState state = { NULL, str, debug, -1, 0, 0, 0, &warnings, &errors };
    AstNode *ast = parse(&state);
    return make_parse_result(ast, warnings, errors);
}

ParseResult *parse_file(FILE *fp, int debug, int sep)
{
    ParseMessage *warnings = NULL, *errors = NULL;
    ParseState state = { fp, NULL, debug, sep, 0, 0, 0, &warnings, &errors };
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
    free(result);
}
