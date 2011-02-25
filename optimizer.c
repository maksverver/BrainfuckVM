#include "optimizer.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static void drop(AstNode **p)
{
    AstNode *next = (*p)->next;
    (*p)->next = NULL;
    ast_free(*p);
    *p = next;
}

/* Recursive optimization, that:
    1. collapses consecutive move operations into one.
    2. collapses consecutive add operations into one.
    3. discards loops that will never be entered, i.e.
        - at the start of the program
        - immediately following another loop
    4. discards code after provably unterminating loops

   Takes a cell value at the start, and returns a cell value at exit, which
   is either 0 (zero), 1 (nonzero) or -1 (unknown). */
static int pass1(AstNode **p, int cell)
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

                if ((*p)->type == OP_MOVE)
                {
                    cell = -1;
                }
                else  /* (*p)->type == OP_ADD */
                {
                    cell = (cell == 0) ? 1 : -1;
                }
            }
            else
            {
                /* Other operation makes cell value unknown: */
                assert((*p)->type == OP_CALL);
                cell = -1;
            }
        }
        else  /* (*p)->type == OP_LOOP */
        {
            if (cell == 0)
            {
                drop(p);
                continue;
            }
            else
            {
                int end = pass1(&(*p)->child, 1);
                if (cell == 1 && end == 1)
                {
                    /* Infinite loop detected! */
                    ast_free((*p)->next);
                    (*p)->next = NULL;
                    cell = 1;
                }
                else
                {
                    /* After loop, cell will be zero. */
                    cell = 0;
                }
            }
        }

        p = &(*p)->next;
    }
    return cell;
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
static AstNode *pass3_collapse(AstNode *p)
{
    AstNode *n, *node;
    SourceSpan origin;
    int begin, end, pos;

    /* Determine bounds: */
    origin.begin = p->origin.begin;
    begin = 0, end = 1, pos = 0;
    for (n = p; n != NULL; n = n->next)
    {
        if (n->next == NULL) origin.end = n->origin.end;
        if (n->type != OP_MOVE) continue;
        pos += n->value;
        if (pos >= end) end = pos + 1;
        else if (pos < begin) begin = pos;
    }

    /* Initialize complex node structure: */
    node = malloc(sizeof(AstNode) + (end - begin));
    assert(node != NULL);
    node->next   = NULL;
    node->child  = NULL;
    node->type   = OP_ADD_MOVE;
    node->value  = pos;
    node->origin = origin;
    node->begin  = begin;
    node->end    = end;
    node->add    = (signed char*)node + sizeof(AstNode);
    memset(node->add, 0, end - begin);
    node->add -= begin;

    /* Determine added values: */
    pos = 0;
    for (n = p; n != NULL; n = n->next)
    {
        if (n->type == OP_MOVE) pos += n->value;
        else node->add[pos] += n->value;
    }

    /* Compress range [begin:end) as far as possible, but ensure all non-zero
       entries are kept in range, as well as the start and end head position: */
    while (node->begin < 0 && node->begin < node->value &&
           node->add[node->begin] == 0) ++node->begin;
    while (node->end > 1 && node->end - 1 > node->value &&
           node->add[node->end - 1] == 0) --node->end;

    return node;
}

/* Recursive optimization that collapses consecutive move/add sequences into a
   single AddMove expression, to allow more efficient code to be generated. */
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
