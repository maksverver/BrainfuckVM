#define _GNU_SOURCE
#include "vm.h"
#include "elf-dumper.h"
#include "debugger.h"
#include "codebuf.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <sys/mman.h>

#if __x86_64
#define IP   REG_RIP
#define HEAD REG_RAX
#define LONGPREFIX 0x48,
#elif __i386
#define IP   REG_EIP
#define HEAD REG_EAX
#define LONGPREFIX
#else
#error "Could not determine target machine type."
#endif

static int pagesize;
static struct sigaction oldact_sigsegv;
static Cell *tape;
static size_t tape_size;            /* in bytes */
static size_t max_tape_size;        /* in bytes */
static int eof_value;               /* negative for none */
static int wrap_check;
static CodeBuf code;
static FILE *input, *output;
static int cell_value;  /* used during code generation */
static int zf_valid;    /* used during code generation */

static void range_check(Cell *cell, Cell **head)
{
    if (cell < tape)
    {
        fprintf(stderr, "tape head exceeds left bound!\n");
        debug_break(head);
        assert(tape - cell < pagesize);
        exit(1);
    }
    if (cell >= tape + tape_size)
    {
        assert(cell - (tape + tape_size) < pagesize);
        vm_expand(head);
    }
}

static Cell *vm_read(Cell *head)
{
    int c;
    range_check(head, &head);
    if ((c = getc(input)) != EOF) *head = c;
    else if (eof_value >= 0) *head = (Cell)eof_value;
    return head;
}

static Cell *vm_write(Cell *head)
{
    range_check(head, &head);
    putc(*head, output);
    return head;
}

static Cell *vm_debug(Cell *head)
{
    range_check(head, &head);
    debug_break(&head);
    return head;
}

static Cell *vm_wrapped(Cell *head)
{
    fprintf(stderr, "cell value wrapped around!\n");
    return vm_debug(head);
}

VM_Callback vm_callbacks[CB_COUNT] = {
    &vm_read, &vm_write, &vm_debug, &vm_wrapped };

static void sigsegv_handler(int signum, siginfo_t *info, void *ucontext_arg)
{
    ucontext_t *uc = (ucontext_t*)ucontext_arg;
    char *ip;
    Cell *head;

    if (signum != SIGSEGV) return;

    ip = (char*)uc->uc_mcontext.gregs[IP];
    if (ip < code.data || ip >= code.data + code.size)
    {
        /* Segmentation fault occured outside of generated program code: */
        fprintf(stderr, "segmentation fault occured!\n");
        abort();
    }

    /* Range check on error address, update RAX and restart: */
    head = (Cell*)uc->uc_mcontext.gregs[HEAD];
    range_check((Cell*)info->si_addr, &head);
    uc->uc_mcontext.gregs[HEAD] = (greg_t)head;
    return;
}

static void gen_bound_check(void)
{
    /* testb $0, (%rax) */
    static const char text[] = { 0xf6, 0x00, 0x00 };
    cb_append(&code, text, sizeof(text));
}

static void gen_large_move(int dist)
{
    /* addq $<dist>, %rax */
    const char text[] = { LONGPREFIX 0x05,
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
    {   /* add <dist>, %rax */
        const char text[] = { LONGPREFIX 0x83, 0xc0, dist };
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
            const char prefix[] = {
                0x74, dist1 };          /* jz <d1> */
            cb_insert(&code, prefix, sizeof(prefix), start);
        } break;
    case 5:
        {
            const char prefix[] = {
                0x80, 0x38, 0x00,       /* cmpb $0, (%rax) */
                0x74, dist1 };          /* jz <d1> */
            cb_insert(&code, prefix, sizeof(prefix), start);
        } break;
    case 6:
        {
            const char prefix[] = {
                0x0f, 0x84,             /* jz .. */
                dist1 >> 0, dist1 >> 8, dist1 >> 16, dist1 >> 24 };
            cb_insert(&code, prefix, sizeof(prefix), start);
        } break;
    case 9:
        {
            const char prefix[] = {
                0x80, 0x38, 0x00,       /* cmpb $0, (%rax) */
                0x0f, 0x84,             /* jz .. */
                dist1 >> 0, dist1 >> 8, dist1 >> 16, dist1 >> 24 };
            cb_insert(&code, prefix, sizeof(prefix), start);
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
            const char suffix[] = {
                0x75, dist2 };          /* jnz <d2> */
            cb_append(&code, suffix, sizeof(suffix));
        } break;
    case 5:
        {
            const char suffix[] = {
                0x80, 0x38, 0x00,       /* cmpb $0, (%rax) */
                0x75, dist2 };          /* jnz <d2> */
            cb_append(&code, suffix, sizeof(suffix));
        } break;
    case 6:
        {
            const char suffix[] = {
                0x0f, 0x85,             /* jnz .. */
                dist2 >> 0, dist2 >> 8, dist2 >> 16, dist2 >> 24 };
            cb_append(&code, suffix, sizeof(suffix));
        } break;
    case 9:
        {
            const char suffix[] = {
                0x80, 0x38, 0x00,       /* cmpb $0, (%rax) */
                0x0f, 0x85,             /* jnz .. */
                dist2 >> 0, dist2 >> 8, dist2 >> 16, dist2 >> 24 };
            cb_append(&code, suffix, sizeof(suffix));
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
static int gen_special_loop_code(AstNode *child)
{
    int pos, num_bits = 0;

    if (child == NULL || child->next != NULL ||   /* must be a single node */
        child->type != OP_ADD_MOVE ||             /* must be Add+Move node */
        child->value != 0) return 0;        /* must not move the tape head */

    /* Verify current cell changes by exactly 1 every step: */
    if (child->add[0] != -1 && child->add[0] != 1) return 0;

    /* Figure out maximum number of bits to be used: */
    for (pos = child->begin; pos < child->end; ++pos)
    {
        if (pos != 0)
        {
            int v = child->add[pos], i = 0;
            if (v < 0) v = -v;
            while (v != 0) ++i, v >>= 1;
            if (i > num_bits) num_bits = i;
        }
    }

    if (num_bits > 0)
    {
        int bit;

        static const char text[] = { 0x8a, 0x08 };  /* movb (%rax), %cl */
        cb_append(&code, text, sizeof(text));

        for (bit = 0; bit < num_bits; ++bit)
        {
            if (bit > 0)
            {
                static const char text[] = { 0x00, 0xc9 };  /* addb %bl, %cl */
                cb_append(&code, text, sizeof(text));
            }
            for (pos = child->begin; pos < child->end; ++pos)
            {
                if (pos != 0)
                {
                    int v = child->add[pos] / -child->add[0];
                    if (v >= 0 && (v & (1 << bit)))
                    {
                        if (pos >= -128 && pos < 128)
                        {   /* addb %cl, <pos>(%rax) */
                            const char text[] = { 0x00, 0x48, pos };
                            cb_append(&code, text, sizeof(text));
                        }
                        else
                        {   /* addb %cl, <pos>(%rax) */
                            const char text[] = { 0x00, 0x88,
                                pos >> 0, pos >> 8, pos >> 16, pos >> 24 };
                            cb_append(&code, text, sizeof(text));
                        }
                    }
                    else
                    if (v < 0 && (-v & (1 << bit)))
                    {
                        if (pos >= -128 && pos < 128)
                        {   /* subb %cl, <pos>(%rax) */
                            const char text[] = { 0x28, 0x48, pos };
                            cb_append(&code, text, sizeof(text));
                        }
                        else
                        {   /* subb %cl, <pos>(%rax) */
                            const char text[] = { 0x28, 0x88,
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
        /* Alternatively, I could use andb $0, (%rax) to achieve the same result
          while updating the zero-flag which may allow me to generate less code.
          However, this is a slightly less efficient operation, and it is
          unlikely this instruction will be followed by a test anyway. */
        char text[] = { 0xc6, 0x00, 0x00 }; /* movb $0, (%rax) */
        cb_append(&code, text, sizeof(text));
    }

    cell_value = 0;
    zf_valid   = 0;

    return 1;
}

static void gen_call(int index)
{
#if __x86_64
    char text[] = { LONGPREFIX 0x89, 0xc7 };  /* movq  %rax, %rdi */
#elif __i386
    char text[] = { 0x50 };                   /* pushl %eax */
#endif
    cb_append(&code, text, sizeof(text));
    assert(index >= 0 && index < CB_COUNT);
    if (index == 0)
    {   /* call *(%rbx) */
        static const char text[] = { 0xff, 0x13 };
        cb_append(&code, text, sizeof(text));
    }
    else
    {   /* call *<offset>(%rbx) */
        char text[] = { 0xff, 0x53, sizeof(void*)*index };
        cb_append(&code, text, sizeof(text));
    }
#if __i386
    {
        char text[] = { 0x83, 0xc4, 0x04 };   /* addl $4, %esp */
        cb_append(&code, text, sizeof(text));
    }
#endif
    cell_value = -1;
    zf_valid   =  0;
}

static void gen_code(AstNode *node)
{
    for ( ; node != NULL; node = node->next)
    {
        switch (node->type)
        {
        case OP_LOOP:
            if (!gen_special_loop_code(node->child))
            {
                gen_loop_code(node);
            }
            break;

        case OP_ADD:
            if ((char)node->value != 0)
            {
                if (node->value >= 0)
                {   /* addb <value>, (%rax) */
                    char text[] = { 0x80, 0x00, (char)node->value };
                    cb_append(&code, text, sizeof(text));
                }
                else  /* node->value < 0 */
                {   /* subb <-value>, (%rax) */
                    char text[] = { 0x80, 0x28, (char)-node->value };
                    cb_append(&code, text, sizeof(text));
                }
                cell_value = (cell_value == 0) ? 1 : -1;
                zf_valid = 1;
                if (wrap_check)
                {
                    int start = code.size;
                    gen_call(CB_WRAPPED);
                    if (node->value > -256 && node->value < 256)
                    {   /* jnc <dist> */
                        const char text[] = { 0x73, code.size - start };
                        cb_insert(&code, text, sizeof(text), start);
                    }
                }
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
            gen_call(node->value);
            break;

        case OP_ADD_MOVE:
            {
                int pos;

                /* FIXME: should try to trigger page faults on page boundaries
                          if bounds exceed pagesize but move to node->offset
                          doesn't cover it*/
                assert(node->begin >= -pagesize && node->end - 1 <= pagesize);

                for (pos = node->begin; pos < node->end; ++pos)
                {
                    if (pos == node->value || node->add[pos] == 0) continue;
                    if (pos == 0)
                    {   /* addb $<value>, (%rax) */
                        const char text[] = { 0x80, 0x00, node->add[pos] };
                        cb_append(&code, text, sizeof(text));
                    }
                    else
                    if (pos >= -128 && pos < 128)
                    {   /* addb $<value>, <pos>(%rax) */
                        const char text[] = { 0x80, 0x40,
                            pos, node->add[pos] };
                        cb_append(&code, text, sizeof(text));
                    }
                    else
                    {   /* addb $<value>, <pos>(%rax) */
                        const char text[] = { 0x80, 0x80,
                            pos, pos >> 8, pos >> 16, pos >> 24,
                            node->add[pos] };
                        cb_append(&code, text, sizeof(text));
                    }
                }

                gen_move(node->value);

                /* Do addition to current head position last, so we benefit
                   from having an up-to-date zero flag: */
                if (node->add[node->value] != 0)
                {
                    /* addb $<value>, (%rax) */
                    const char text[] = { 0x80, 0x00, node->add[node->value] };
                    cb_append(&code, text, sizeof(text));
                    zf_valid = 1;
                }
                else
                {
                    zf_valid = 0;
                }

                if (node->value != 0)
                {
                    cell_value = -1;  /* head moved */
                }
                else
                if (node->add[node->value] != 0) 
                {
                    cell_value = (cell_value == 0) ? 1 : -1;  /* cell changed */
                }  /* else: head and cell value unchanged */

            } break;

        default:
            assert(0);
        }
    }
}

static void gen_func(AstNode *ast)
{
    static const char prologue[] = {
        0x55,                       /* push %rbp */
        LONGPREFIX 0x89, 0xe5,      /* movq %rsp, %rbp */
        0x53,                       /* pushq %rbx */
#if __x86_64
        LONGPREFIX 0x89, 0xf8,      /* movq %rdi, %rax */
        LONGPREFIX 0x89, 0xf3 };    /* movq %rsi, %rbx */
#elif __i386
        0x8b, 0x45, 0x08,           /* mov  8(%ebp), %eax */
        0x8b, 0x5d, 0x0c };         /* mov 12(%ebp), %ebx */
#endif

    static const char epilogue[] = {
        0x5b,                   /* popq %rbx */
        0x5d,                   /* popq %ebp */
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
    if (data == MAP_FAILED)
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
        if (tape == MAP_FAILED)
        {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        assert((char*)tape == start);
    }
    tape_size = size;
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
    eof_value = -1;
    vm_alloc(0);
}

void vm_load(AstNode *ast)
{
    cb_truncate(&code);
    gen_func(ast);
}

void vm_set_input(FILE *fp)
{
    assert(fp != NULL);
    input = fp;
}

void vm_set_output(FILE *fp)
{
    assert(fp != NULL);
    output = fp;
}

void vm_set_memlimit(size_t size)
{
    if (size < (size_t)pagesize)
    {
        fprintf(stderr, "memory limit too small (minimum: %d bytes)\n",
                        pagesize);
        exit(1);
    }
    max_tape_size = size;
}

void vm_set_eof_value(int val)
{
    assert(val == -1 || (val&~0xff) == 0);
    eof_value = val;
}

void vm_set_wrap_check(int val)
{
    assert(val == 0 || val == 1);
    wrap_check = val;
}

void vm_exec(void)
{
    Cell *head = tape;
    head = ((Cell *(*)(Cell*, VM_Callback*))code.data)(head, vm_callbacks);
    range_check(head, &head);
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

void vm_expand(Cell **head)
{
    size_t head_pos = (head == NULL) ? 0 : *head - tape;
    assert(tape_size >= (size_t)pagesize);
    vm_alloc(tape_size + (tape_size/pagesize + 3)/4*pagesize);
    if (head != NULL) *head = tape + head_pos;
}

Cell *vm_memory(size_t *size)
{
    if (size != NULL) *size = tape_size;
    return tape;
}
