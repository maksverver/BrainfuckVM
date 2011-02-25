#include "ast.h"
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

AstNode *ast_clone(AstNode *node)
{
    AstNode *copy;

    if (node == NULL) return NULL;

    /* Only support cloning basic nodes for now: */
    assert(node->type > OP_NONE && node->type < OP_ADD_MOVE);

    /* Allocate memory: */
    copy = malloc(offsetof(AstNode, begin));
    assert(copy != NULL);

    /* Clone/copy fields: */
    copy->next   = ast_clone(node->next);
    copy->child  = ast_clone(node->child);
    copy->type   = node->type;
    copy->value  = node->value;
    copy->code   = node->code;
    copy->origin = node->origin;

    return copy;
}

void ast_free(AstNode *node)
{
    while (node != NULL)
    {
        AstNode *next = node->next;
        ast_free(node->child);
        free(node);
        node = next;
    }
}

static void print_tree(const AstNode *node, int depth, FILE *fp)
{
    static const char *types[6] = {
        "NONE", "LOOP", "ADD", "MOVE", "CALL", "ADD_MOVE" };
    int d;

    while (node != NULL)
    {
        for (d = 0; d < depth; ++d) fputc('\t', fp);
        fprintf(fp, "%s %d origin=[%d:%d,%d:%d] code=[%llxh,%llxh)\n",
            (unsigned)node->type < 6 ? types[node->type] : "<INVALID>",
            node->value,
            SRCLOC_LINE(node->origin.begin), SRCLOC_COLUMN(node->origin.begin),
            SRCLOC_LINE(node->origin.end), SRCLOC_COLUMN(node->origin.end),
            (long long)node->code.begin, (long long)node->code.end);
        print_tree(node->child, depth + 1, fp);
        node = node->next;
    }
}

void ast_print_tree(const AstNode *root, FILE *fp)
{
    print_tree(root, 0, fp);
}
