#define _GNU_SOURCE
#include "vm.h"
#include "elf-dumper.h"
#include "codebuf.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <sys/mman.h>

static int pagesize;
static struct sigaction oldact_sigsegv;
static Cell *tape;
static size_t tape_size;            /* in bytes */
static size_t max_tape_size;        /* in bytes */
static CodeBuf code;
static int cell_value;  /* used during code generation */
static int zf_valid;    /* used during code generation */

/* Debug-prints the current head position and context data.
   Accepts out-of-range head positions. */
static Cell *debug_print(Cell *head)
{
    ssize_t min, pos, max, i;

    pos = head - tape;
    if (pos - 10 < 0)
    {
        min = 0;
        max = 20;
    }
    else
    if (pos + 10 > (ssize_t)tape_size - 1)
    {
        min = tape_size - 21;
        max = tape_size - 1;
    }
    else
    {
        min = pos - 10;
        max = pos + 10;
    }

    fprintf(stderr, "%3d[%3d]%3d:", (int)min, (int)pos, (int)max);
    for (i = min; i <= max; ++i)
    {
        fputc((i == pos) ? '[' : (i == pos + 1) ? ']' : ' ', stderr);
        fprintf(stderr, "%02x", tape[i]);
    }
    if (i == pos + 1) fputc(']', stderr);
    fputc('\n', stderr);
    return head;
}

static void vm_expand(void);

static void range_check(Cell *head)
{
    if (head < tape)
    {
        fprintf(stderr, "tape head exceeds left bound!\n");
        debug_print(head);
        assert(tape - head < pagesize);
        exit(1);
    }
    if (head >= tape + tape_size)
    {
        assert(head - (tape + tape_size) < pagesize);
        vm_expand();
    }
}

static Cell *vm_read(Cell *head)
{
    int c;
    range_check(head);
    if ((c = getchar()) != EOF) *head = c;
    return head;
}

static Cell *vm_write(Cell *head)
{
    range_check(head);
    putchar(*head);
    return head;
}

static Cell *vm_debug(Cell *head)
{
    range_check(head);
    debug_print(head);
    return head;
}

VM_Callback vm_callbacks[CB_COUNT] = { &vm_read, &vm_write, &vm_debug };

static void sigsegv_handler(int signum, siginfo_t *info, void *ucontext_arg)
{
    ucontext_t *uc = (ucontext_t*)ucontext_arg;
    size_t code_pos, data_pos;

    if (signum != SIGSEGV) return;

    code_pos = (size_t)((char*)uc->uc_mcontext.gregs[REG_RIP] - code.data);
    if (code_pos >= code.size)
    {
        /* Segmentation fault occured outside of generated program code: */
        fprintf(stderr, "segmentation fault occured!\n");
        abort();
    }

    /* Range check on error address, update RAX and restart: */
    data_pos = (Cell*)uc->uc_mcontext.gregs[REG_RAX] - tape;
    range_check((Cell*)info->si_addr);
    uc->uc_mcontext.gregs[REG_RAX] = (greg_t)(tape + data_pos);
    return;
}

static void gen_bound_check(void)
{
    /* testb $0, (%rax) */
    static const char text[3] = { 0xf6, 0x00, 0x00 };
    cb_append(&code, text, sizeof(text));
}

static void gen_large_move(int dist)
{
    /* addq $<dist>, %rax */
    const char text[6] = { 0x48, 0x05,
                           dist >> 0, dist >> 8, dist >> 16, dist >> 24 };
    cb_append(&code, text, sizeof(text));
}

static void gen_move(int dist)
{
    if (dist > pagesize)
    {
        do {
            gen_large_move(pagesize);
            gen_bound_check();
            dist -= pagesize;
        } while (dist > pagesize);
    }
    else
    if (dist < -pagesize)
    {
        do {
            gen_large_move(-pagesize);
            gen_bound_check();
            dist += pagesize;
        } while (dist < -pagesize);
    }

    if (dist == 0)
    {
        /* nop */
    }
    else
    if (dist >= -128 && dist < 128)
    {
        const char text[4] = { 0x48, 0x83, 0xc0, dist };
        cb_append(&code, text, sizeof(text));
    }
    else
    {
        gen_large_move(dist);
    }
}

static void gen_code(AstNode *node);

static void gen_loop_code(AstNode *node)
{
    size_t start, size;
    int gen1, gen2, dist1, dist2, size1, size2;

    size1 = zf_valid ? 0 : 3;
    gen1  = (cell_value == 1) ? 0 : 1;

    /* Assume zero-flag valid before generating child code, because either we
       will generate a prefix that uses but not changes the zero flag, or we
       will elide the prefix, but in that case a conditional jump in the child
       code will be elided as well: */
    zf_valid   = 1;
    cell_value = 1;   /* head will be at non-zero at start of loop */

    start = code.size;
    gen_code(node->child);
    size = code.size - start;

    size2 = zf_valid ? 0 : 3;
    gen2  = (cell_value == 0) ? 0 : 1;

    /* As above, we either used but not changed zf in the conditional jump or
       the entire suffix was elided, in which case any further conditional jumps
       will be elided as well. */
    cell_value = 0;  /* head will be at zero cell after loop */
    zf_valid   = 1;

    /* Determine jump length and code size of suffix: */
    size2 += ((int)size + size2 + 2 <= 128) ? 2 : 6;
    dist2 = -(int)size - size2;
    if (!gen2) size2 = 0;

    /* Determine  jump length and code size of prefix:
        (It's not a mistake that I use size2 here too; both the
        prefix and suffix code jump over the suffix code only.) */
    size1 += ((int)size + size2 + 2 <= 127 ? 2 : 6);
    dist1 = (int)size + size2;
    if (!gen1) size1 = 0;

    /* Insert prefix test + conditional jump: */
    switch (size1)
    {
    case 0:
        break;
    case 2:
        {
            const char prefix[2] = {
                0x74, dist1 };          /* jz <d1> */
            cb_insert(&code, prefix, 2, start);
        } break;
    case 5:
        {
            const char prefix[5] = {
                0x80, 0x38, 0x00,       /* cmp $0, (%rax) */
                0x74, dist1 };          /* jz <d1> */
            cb_insert(&code, prefix, 5, start);
        } break;
    case 6:
        {
            const char prefix[6] = {
                0x0f, 0x84,             /* jz .. */
                dist1 >> 0, dist1 >> 8, dist1 >> 16, dist1 >> 24 };
            cb_insert(&code, prefix, 6, start);
        } break;
    case 9:
        {
            const char prefix[9] = {
                0x80, 0x38, 0x00,       /* cmp $0, (%rax) */
                0x0f, 0x84,             /* jz .. */
                dist1 >> 0, dist1 >> 8, dist1 >> 16, dist1 >> 24 };
            cb_insert(&code, prefix, 9, start);
        } break;
    default:
        assert(0);
        break;
    }

    /* Insert suffix test + conditional jump: */
    switch (size2)
    {
    case 0:
        break;
    case 2:
        {
            const char suffix[2] = {
                0x75, dist2 };          /* jnz <d2> */
            cb_append(&code, suffix, 2);
        } break;
    case 5:
        {
            const char suffix[5] = {
                0x80, 0x38, 0x00,       /* cmp $0, (%rax) */
                0x75, dist2 };          /* jnz <d2> */
            cb_append(&code, suffix, 5);
        } break;
    case 6:
        {
            const char suffix[6] = {
                0x0f, 0x85,             /* jnz .. */
                dist2 >> 0, dist2 >> 8, dist2 >> 16, dist2 >> 24 };
            cb_append(&code, suffix, 6);
        } break;
    case 9:
        {
            const char suffix[9] = {
                0x80, 0x38, 0x00,       /* cmp $0, (%rax) */
                0x0f, 0x85,             /* jnz .. */
                dist2 >> 0, dist2 >> 8, dist2 >> 16, dist2 >> 24 };
            cb_append(&code, suffix, 9);
        } break;
    default:
        assert(0);
        break;
    }
}

/* Generate special-case loop code when a loop contains only a single AddMove
   node and nothing else, does not (effectively) move the tape head, and the
   value of the tape head is decremented or incremented by one every iteration.
   In this case, we can eliminate the loop entirely and generate static code
   that adds a constant multiple of the current cell to the affected cells. */
static int gen_special_loop_code(AddMoveNode *child)
{
    int pos, step = child->add[-child->begin], num_bits = 0;

    if ( child->base.next != NULL ||     /* must have a single child node */
         child->offset != 0 ||           /* must not move the tape head */
         (step != 1 && step != -1))       /* must change by one each time */
    {
        return 0;
    }

    /* Figure out maximum number of bits to be used: */
    for (pos = child->begin; pos < child->end; ++pos)
    {
        if (pos != 0)
        {
            int v = child->add[pos - child->begin], i = 0;
            if (v < 0) v = -v;
            while (v != 0) ++i, v >>= 1;
            if (i > num_bits) num_bits = i;
        }
    }

    if (num_bits > 0)
    {
        int bit;

        static const char text[2] = { 0x8a, 0x08 };  /* movb (%rax), %bl */
        cb_append(&code, text, sizeof(text));

        for (bit = 0; bit < num_bits; ++bit)
        {
            if (bit > 0)
            {
                static const char text[2] = { 0x00, 0xc9 };  /* addb %bl, %bl */
                cb_append(&code, text, sizeof(text));
            }
            for (pos = child->begin; pos < child->end; ++pos)
            {
                if (pos != 0)
                {
                    int v = child->add[pos - child->begin] / -step;
                    if (v >= 0 && (v & (1 << bit)))
                    {
                        if (pos >= -128 && pos < 128)
                        {   /* addb %cl, <pos>(%rax) */
                            const char text[3] = { 0x00, 0x48, pos };
                            cb_append(&code, text, sizeof(text));
                        }
                        else
                        {   /* addb %cl, <pos>(%rax) */
                            const char text[6] = { 0x00, 0x88,
                                pos >> 0, pos >> 8, pos >> 16, pos >> 24 };
                            cb_append(&code, text, sizeof(text));
                        }
                    }
                    else
                    if (v < 0 && (-v & (1 << bit)))
                    {
                        if (pos >= -128 && pos < 128)
                        {   /* subb %cl, <pos>(%rax) */
                            const char text[3] = { 0x28, 0x48, pos };
                            cb_append(&code, text, sizeof(text));
                        }
                        else
                        {   /* subb %cl, <pos>(%rax) */
                            const char text[6] = { 0x28, 0x88,
                                pos >> 0, pos >> 8, pos >> 16, pos >> 24 };
                            cb_append(&code, text, sizeof(text));
                        }
                    }
                }
            }
        }
    }

    /* Finally, clear current cell: */
    {
        /* FIXME: alternatively, I could use andb $0, (%rax) to achieve the
            same result while updating the zero-flag which may allow me to
            generate less code. However, maybe this is less efficient? */
        char text[3] = { 0xc6, 0x00, 0x00 }; /* movb $0, (%rax) */
        cb_append(&code, text, sizeof(text));
    }

    cell_value = 0;
    zf_valid   = 0;

    return 1;
}

static void gen_code(AstNode *node)
{
    for ( ; node != NULL; node = node->next)
    {
        switch (node->type)
        {
        case OP_LOOP:
            if (node->child != NULL && node->child->type == OP_ADD_MOVE &&
                gen_special_loop_code((AddMoveNode*)node->child))
            {
                /* we're done. */
            }
            else
            {
                gen_loop_code(node);
            }
            break;

        case OP_ADD:
            if ((char)node->value != 0)
            {   /* addb <value>, (%rax) */
                const char text[3] = { 0x80, 0x00, (char)node->value };
                cb_append(&code, text, sizeof(text));
                cell_value = (cell_value == 0) ? 1 : -1;
                zf_valid = 1;
            }
            break;

        case OP_MOVE:
            if (node->value != 0)
            {
                gen_move(node->value);

                /* Test validity of head position between moves: */
                if (node->next != NULL && node->next->type == OP_MOVE)
                {
                    gen_bound_check();
                }

                cell_value = -1;
                zf_valid   =  0;
            }
            break;

        case OP_CALL:
            {   /* movq  %rax, %rdi */
                char text[3] = { 0x48, 0x89, 0xc7 };
                cb_append(&code, text, sizeof(text));
                assert(node->value >= 0 && node->value < CB_COUNT);
                if (node->value == 0)
                {   /* call *(%rbx) */
                    static const char text[2] = { 0xff, 0x13 };
                    cb_append(&code, text, sizeof(text));
                }
                else
                {   /* call *val(%rbx) */
                    char text[3] = { 0xff, 0x53, 8*node->value };
                    cb_append(&code, text, sizeof(text));
                }
                cell_value = -1;
                zf_valid   =  0;
            } break;

        case OP_ADD_MOVE:
            {
                AddMoveNode *man = (AddMoveNode*)node;
                int n, add, pos;

                /* FIXME: should try to trigger page faults on page boundaries
                          if bounds exceed pagesize but move to man->offset
                          doesn't cover it*/
                assert(man->begin >= -pagesize && man->end - 1 <= pagesize);

                gen_move(man->offset);

                for (n = 0; n < man->end - man->begin; ++n)
                {
                    if ((add = man->add[n]) != 0)
                    {
                        pos = man->begin + n - man->offset;
                        if (pos == 0)
                        {
                            /* defer */
                        }
                        else
                        if (pos >= -128 && pos < 128)
                        {   /* addb $<value>, <pos>(%rax) */
                            const char text[4] = { 0x80, 0x40, pos, add };
                            cb_append(&code, text, sizeof(text));
                        }
                        else
                        {   /* addb $<value>, <pos>(%rax) */
                            const char text[7] = { 0x80, 0x80,
                                pos, pos >> 8, pos >> 16, pos >> 24, add };
                            cb_append(&code, text, sizeof(text));
                        }
                    }
                }

                /* Do addition to current head position last, so we benefit
                   from having an up-to-date zero flag: */
                if ((add = man->add[man->offset - man->begin]))
                {
                    /* addb $<value>, (%rax) */
                    const char text[3] = { 0x80, 0x00, add };
                    cb_append(&code, text, sizeof(text));
                    zf_valid = 1;
                }
                else
                {
                    zf_valid = 0;
                }

                if (man->offset != 0) cell_value = -1;
                else if (add != 0) cell_value = (cell_value == 0) ? 1 : -1;
            } break;

        default:
            assert(0);
        }
    }
}

static void gen_func(AstNode *ast)
{
    static const char prologue[7] = {
        0x53,                   /* pushq %rbx */
        0x48, 0x89, 0xf8,       /* movq %rdi, %rax */
        0x48, 0x89, 0xf3 };     /* movq %rsi, %rbx */
    static const char epilogue[2] = {
        0x5b,                   /* popq %rbx */
        0xc3 };                 /* ret */

    /* cell_value keeps track of the value in the cell under the tape head
       during code generation; 0 for known zero, 1 for known non-zero, -1 if
       unknown. In particular, cell_value is 1 at the start of a loop, 0 at the
       end of a loop, and -1 after most other operations. */
    cell_value = 0;

    /* zf_valid keeps track of whether the zero-flag correctly indicates the
       whether the value under the tape head is currently zero. */
    zf_valid = 0;

    cb_append(&code, prologue, sizeof(prologue));
    gen_code(ast);
    gen_bound_check();
    cb_append(&code, epilogue, sizeof(epilogue));
}

static void vm_free()
{
    int res;
    if (tape == NULL) return;
    res = munmap((char*)tape - pagesize, tape_size + 2*pagesize);
    assert(res == 0);
    tape = NULL;
    tape_size = 0;
}

static void vm_alloc(size_t size)
{
    char *data, *start;

    if (size == 0) size = pagesize;
    if (size%pagesize != 0) size += pagesize - size%pagesize;

    if (max_tape_size > 0 && size > max_tape_size)
    {
        fprintf(stderr, "memory limit exceeded\n");
        exit(1);
    }

    data = mmap(NULL, size + 2*pagesize, PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == NULL)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    start = data + pagesize;

    if (tape == NULL)
    {
        tape = mmap(start, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        assert((char*)tape == start);
    }
    else
    {
        int res;
        res = munmap((char*)tape - pagesize, pagesize);
        assert(res == 0);
        res = munmap((char*)tape + tape_size, pagesize);
        assert(res == 0);
        tape = mremap(tape, tape_size,
                      size, MREMAP_MAYMOVE | MREMAP_FIXED, start);
        assert((char*)tape == start);
    }
    tape_size = size;
}

static void vm_expand(void)
{
    assert(tape_size >= (size_t)pagesize);
    vm_alloc(tape_size + (tape_size/pagesize + 3)/4*pagesize);
}

void vm_init(void)
{
    struct sigaction sigact_sigsegv;
    pagesize = getpagesize();
    sigact_sigsegv.sa_flags     = SA_SIGINFO;
    sigact_sigsegv.sa_sigaction = &sigsegv_handler;
    sigemptyset(&sigact_sigsegv.sa_mask);
    sigaction(SIGSEGV, &sigact_sigsegv, &oldact_sigsegv);
    cb_create(&code);
    assert(CB_COUNT < 32);
    max_tape_size = 0;
    vm_alloc(0);
}

void vm_load(AstNode *ast)
{
    cb_truncate(&code);
    gen_func(ast);
}

void vm_limit_mem(size_t size)
{
    if (size < (size_t)pagesize)
    {
        fprintf(stderr, "memory limit too small (minimum: %d bytes)\n",
                        pagesize);
        exit(1);
    }
    max_tape_size = size;
}

void vm_exec(void)
{
    Cell *pos = ((Cell *(*)(Cell*, VM_Callback*))code.data)(tape, vm_callbacks);
    range_check(pos);
}

void vm_fini(void)
{
    sigaction(SIGSEGV, &oldact_sigsegv, NULL);
    vm_free();
    cb_destroy(&code);
}

void vm_dump(FILE *fp)
{
    elf_dump(fp, code.data, code.size);
}
