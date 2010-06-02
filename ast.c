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
    copy->next  = ast_clone(node->next);
    copy->child = ast_clone(node->child);
    copy->type  = node->type;
    copy->value = node->value;

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
