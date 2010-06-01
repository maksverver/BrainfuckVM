#include "optimizer.h"
#include <assert.h>
#include <stdlib.h>

static void drop(AstNode **p)
{
    AstNode *next = (*p)->next;
    (*p)->next = NULL;
    ast_free(*p);
    *p = next;
}

/* Recursive optimization, that:
    1. discards loops that will never be entered, i.e.
        - at the start of the program
        - immediately following another loop
    2. collapses consecutive move operations into one.
    3. collapses consecutive add operations into one. */
static void pass1(AstNode **p, int cell)
{
    while (*p != NULL)
    {
        if ((*p)->type != OP_LOOP)
        {
            if ((*p)->type == OP_MOVE || (*p)->type == OP_ADD)
            {
                /* Collapse consecutive operations of identical type: */
                while ((*p)->next != NULL && (*p)->type == (*p)->next->type)
                {
                    (*p)->value += (*p)->next->value;
                    drop(&(*p)->next);
                }

                if ((*p)->type == OP_ADD) (*p)->value = (char)(*p)->value;

                if ((*p)->value == 0)
                {
                    drop(p);
                    continue;
                }
            }

            /* Non-loop operation makes cell value unknown: */
            cell = -1;
        }
        else  /* (*p)->type == OP_LOOP */
        {
            if (cell == 0)
            {
                drop(p);
                continue;
            }
            pass1(&(*p)->child, 1);
            cell = 0;  /* cell will be zero after loop */
        }

        p = &(*p)->next;
    }
}

/* Top-level optimization that removes all code after the last loop (which
   might be infinite) or call operation: */
static void pass2(AstNode **p)
{
    AstNode *cur, **end = p;
    for (cur = *p; cur != NULL; cur = cur->next)
    {
        if (cur->type == OP_LOOP || cur->type == OP_CALL) end = &cur->next;
    }
    ast_free(*end);
    *end = NULL;
}

/* Collapse the given sequence of move/add nodes into a single AddMoveNode: */
static AddMoveNode *pass3_collapse(AstNode *p)
{
    AstNode *n;
    AddMoveNode *node;
    int pos;

    /* Initialize complex node structure: */
    node = malloc(sizeof(AddMoveNode));
    assert(node != NULL);
    node->base.next  = NULL;
    node->base.child = NULL;
    node->base.type  = OP_ADD_MOVE;
    node->base.value = 0;
    node->offset = 0;
    node->begin  = 0;
    node->end    = 1;

    /* Determine bounds: */
    pos = 0;
    for (n = p; n != NULL; n = n->next)
    {
        if (n->type != OP_MOVE) continue;
        pos += n->value;
        if (pos >= node->end) node->end = pos + 1;
        else if (pos < node->begin) node->begin = pos;
    }
    node->offset = pos;

    /* Allocate added values within bounds: */
    node->add = calloc(node->end - node->begin, sizeof(char));

    /* Determine added values: */
    pos = -node->begin;
    for (n = p; n != NULL; n = n->next)
    {
        if (n->type == OP_MOVE) pos += n->value;
        else node->add[pos] += n->value;
    }
    return node;
}

/* Recursive optimization that collapses consecutive move/add sequences of
   three or more operations into a single AddMove expression, in order to
   allow more efficient code to be generated. */
static void pass3(AstNode **p)
{
    while (*p != NULL)
    {
        if ((*p)->type == OP_MOVE || (*p)->type == OP_ADD)
        {
            AstNode **q = &(*p)->next;
            int cnt = 1;
            while (*q != NULL && ((*q)->type == OP_MOVE ||
                                  (*q)->type == OP_ADD))
            {
                ++cnt;
                q = &(*q)->next;
            }
            /* N.B. previously I only collapsed series of more than two nodes,
               which is more efficient and might be easier for the code
               generator to generate efficient code in simple cases. However,
               it turns out it's easier to generate efficient code in a uniform
               manner when instructions are as general as possible. */
            if (1) /* was: (cnt > 2) */
            {
                AstNode *head = *p, *tail = *q;
                *q = NULL;
                *p = (AstNode*)pass3_collapse(head);
                assert((*p)->next == NULL);
                (*p)->next = tail;
                ast_free(head);
            }
            else
            {
                p = q;
            }
        }
        else
        {
            if ((*p)->type == OP_LOOP) pass3(&(*p)->child);
            p = &(*p)->next;
        }
    }
}

AstNode *optimize(AstNode *ast)
{
    pass1(&ast, 0);
    pass2(&ast);
    pass3(&ast);
    return ast;
}
