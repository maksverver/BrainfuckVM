#include "ast-printer.h"
#include <assert.h>

typedef struct PrintContext
{
    FILE    *fp;
    int     printed;
    int     linewidth;
    int     debug;
} PrintContext;

static void print_char(char ch, PrintContext *pc)
{
    fputc(ch, pc->fp);
    if (++pc->printed % pc->linewidth == 0) fputc('\n', pc->fp);
}

static void print_add(int value, PrintContext *pc)
{
    while (value > 0) print_char('+', pc), --value;
    while (value < 0) print_char('-', pc), ++value;
}

static void print_move(int value, PrintContext *pc)
{
    while (value > 0) print_char('>', pc), --value;
    while (value < 0) print_char('<', pc), ++value;
}

static void print_ast(AstNode *node, PrintContext *pc)
{
    if (node == NULL) return;

    switch (node->type)
    {
    case OP_LOOP:
        print_char('[', pc);
        print_ast(node->child, pc);
        print_char(']', pc);
        break;

    case OP_ADD:
        print_add(node->value, pc);
        break;

    case OP_MOVE:
        print_move(node->value, pc);
        break;

    case OP_CALL:
        switch (node->value)
        {
        case 0: print_char(',', pc); break;
        case 1: print_char('.', pc); break;
        case 2: if (pc->debug >= 0) print_char(pc->debug, pc); break;
        default: assert(0);
        }
        break;

    case OP_ADD_MOVE:
        {
            int pos;
            AddMoveNode *man = (AddMoveNode*)node;
            if (man->offset < 0)
            {
                print_move(man->end - 1, pc);
                for (pos = man->end - 1; pos > man->begin; --pos)
                {
                    print_add(man->add[pos - man->begin], pc);
                    print_move(-1, pc);
                }
                print_add(man->add[pos - man->begin], pc);
                print_move(man->offset - pos, pc);
            }
            else  /* man->offset >= 0 */
            {
                print_move(man->begin, pc);
                for (pos = man->begin; pos < man->end - 1; ++pos)
                {
                    print_add(man->add[pos - man->begin], pc);
                    print_move(1, pc);
                }
                print_add(man->add[pos - man->begin], pc);
                print_move(man->offset - pos, pc);
            }
        } break;

    default:
        assert(0);
    }

    print_ast(node->next, pc);
}

void ast_print(AstNode *ast, FILE *fp, int linewidth, int debug)
{
    PrintContext pc = { fp, 0, linewidth, debug };
    print_ast(ast, &pc);
    if (pc.printed % pc.linewidth != 0) fputc('\n', pc.fp);
}
