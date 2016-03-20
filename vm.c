#define _GNU_SOURCE
#include "vm.h"
#include "elf-dumper.h"
#include "debugger.h"
#include "codebuf.h"
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <sys/time.h>

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

static const struct itimerval timer_on  = { { 0, 1000 }, { 0, 1000 } };
static const struct itimerval timer_off = { { 0,    0 }, { 0,    0 } };

static AstNode *program;
static int pagesize;
static struct sigaction oldact_sigsegv;
static struct sigaction oldact_sigint;
static struct sigaction oldact_sigterm;
static struct sigaction oldact_sigvtalrm;
static size_t *profile;
static int interrupted;
static Cell *tape;
static size_t tape_size;            /* in bytes */
static size_t max_tape_size;        /* in bytes */
static int eof_value;               /* negative for none */
static int wrap_check;
static CodeBuf code;
static FILE *input, *output;
static int cell_value;  /* used during code generation */
static int zf_valid;    /* used during code generation */

/* Finds the offset of the instruction after the last one executed in the
   code buffer (if any) or returns 0 if it is not found. */
static size_t find_offset()
{
    void *addrs[32];
    int n, naddr;

    /* Search for origin of error in generated code: */
    naddr = backtrace(&addrs[0], sizeof(addrs)/sizeof(addrs[0]));
    for (n = 0; n < naddr; ++n)
    {
        if ((char*)addrs[n] > code.data &&
            (char*)addrs[n] <= code.data + code.size)
        {
            return (char*)addrs[n] - code.data;
        }
    }
    return 0;
}

static const AstNode *find_closest_node(const AstNode *node, size_t offset)
{
    while (node != NULL)
    {
        if (node->code.begin < offset && offset <= node->code.end)
        {
            const AstNode *result = find_closest_node(node->child, offset);
            return result ? result : node;
        }
        node = node->next;
    }
    return NULL;
}

static void break_to_debugger(Cell **head)
{
    size_t offset = find_offset();
    const AstNode *node = find_closest_node(program, offset);

    debug_break(head, node, offset);
}

static void range_check(Cell **head)
{
    while (*head < tape)
    {
        fprintf(stderr, "tape head exceeds left bound\n");
        break_to_debugger(head);
    }
    if (*head >= tape + tape_size)
    {
        assert(*head - (tape + tape_size) < pagesize);
        vm_expand(head);
    }
}

static Cell *vm_callback(Cell *head, int request)
{
    range_check(&head);
    switch (request)
    {
    case CB_READ:
        {
            int ch = getc(input);
            *head = (ch != EOF) ? ch : (eof_value >= 0) ? eof_value : *head;
        } break;

    case CB_WRITE:
        putc(*head, output);
        break;

    case CB_WRAPPED:
        fprintf(stderr, "cell value wrapped around\n");
    case CB_DEBUG:
        break_to_debugger(&head);
        break;
    }
    if (interrupted)
    {
        break_to_debugger(&head);
        interrupted = 0;
    }
    return head;
}

static void signal_handler(int signum, siginfo_t *info, void *ucontext_arg)
{
    ucontext_t *uc = (ucontext_t*)ucontext_arg;
    char *ip = (char*)uc->uc_mcontext.gregs[IP];
    Cell **head = (ip >= code.data && ip < code.data + code.size) ?
                  (Cell**)&uc->uc_mcontext.gregs[HEAD] : NULL;

    (void)info;  /* unused */

    switch (signum)
    {
    case SIGSEGV:
        if (head)
        {   /* Note that faulting addresss is at info->si_addr, which (in
               optimized code) may be different from *head! */
            Cell *addr = (Cell*)info->si_addr;
            if (addr >= tape + tape_size)
            {   /* Right bound exceeded. Try to expand memory. */
                assert(addr - (tape + tape_size) < pagesize);
                vm_expand(head);
            }
            else
            if (addr < tape)
            {   /* Left bound exceeded. Drop into debugger. */
                fprintf(stderr, "memory access exceeds left bound\n");
                break_to_debugger(head);
            }
            else
            {   /* This should be impossible. */
                fprintf(stderr, "segmentation fault within tape bounds\n");
                abort();
            }
        }
        else
        {   /* Segmentation fault occurred outside of generated program code: */
            fprintf(stderr, "segmentation fault occurred\n");
            abort();
        }
        break;

    case SIGINT:
        if (head)
        {   /* Interrupted generated code; call debugger immediately: */
            break_to_debugger(head);
            interrupted = 0;
        }
        else
        {   /* Generated code not active; signal callback. */
            interrupted = 1;
        }
        break;

    case SIGTERM:
        /* Causes atexit handlers to be called. */
        exit(0);
        break;

    case SIGVTALRM:
        {
            assert(profile);
            ++profile[find_offset()];
            return;  /* optimization: return immediately */
        }
        break;

    default:
        fprintf(stderr, "unexpected signal received: %d\n", signum);
        abort();
    }

    if (head)
    {
        /* Ensure head is valid before continuing. */
        range_check(head);
        /* Ensure zero flag corresponds to : */
        if (*head) {
            uc->uc_mcontext.gregs[REG_EFL] &= ~(1<<6);  /* clear zero flag */
        } else {
            uc->uc_mcontext.gregs[REG_EFL] |=   1<<6;     /* set zero flag */
        }
    }
}

static void check_head(void)
{
    /* cmpb $0, (%rax) */
    static const char text[] = { 0x80, 0x38, 0x00 };
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
            check_head();
            dist -= pagesize;
        } while (dist > pagesize);
    }
    else
    if (dist < -pagesize)
    {
        do {
            gen_large_move(-pagesize);
            check_head();
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

static void move_code(AstNode *node, int dist)
{
    while (node != NULL)
    {
        node->code.begin += dist;
        node->code.end   += dist;
        move_code(node->child, dist);
        node = node->next;
    }
}

static void gen_loop_code(AstNode *node)
{
    size_t start, size;
    int gen1, gen2, dist1, dist2, size1, size2;

    /* Note that we never generate unconditional jumps, even if call_value is
       known, because these correspond with either unreachable loop bodies
       (which are removed by the optimizer) or infinite loops (which do not
       occur except at the top level in sensible programs), so there is
       practically nothing to be gained from handling this case specially. */

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

    /* Adjust code offsets in child nodes for inserted prefix: */
    move_code(node->child, size1);

    /* Append suffix test + conditional jump: */
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

static void gen_call(int request)
{
    char text[] = {
#if __x86_64
        LONGPREFIX 0x89, 0xc7,         /* movq  %rax, %rdi */
        0xbe, (char)request, 0, 0, 0,  /* mov <request>, %esi */
        0xff, 0xd3                     /* call *%rbx */
#elif __i386
        0x6a, (char)request,    /* push <request> */
        0x50,                   /* pushl %eax */
        0xff, 0xd3,             /* call *%ebx */
        0x83, 0xc4, 0x08        /* addl $8, %esp */
#endif
    };
    cb_append(&code, text, sizeof(text));
    cell_value = -1;
    zf_valid   =  0;
}

static void check_wrap(int keep_zf, int conditional)
{
    int start = code.size;
    gen_call(CB_WRAPPED);
    if (keep_zf) check_head();
    if (conditional)
    {   /* jnc <dist> */
        const char text[] = { 0x73, code.size - start };
        cb_insert(&code, text, sizeof(text), start);
    }
}

static void gen_add(int offset, int value)
{
    if (value >= 0)
    {
        if (offset == 0)
        {   /* addb <value>, (%rax) */
            char text[] = { 0x80, 0x00, value };
            cb_append(&code, text, sizeof(text));
        }
        else
        if (offset >= -128 && offset < 128)
        {   /* addb $<value>, <offset>(%rax) */
            const char text[] = { 0x80, 0x40, offset, value };
            cb_append(&code, text, sizeof(text));
        }
        else
        {   /* addb $<value>, <offset>(%rax) */
            const char text[] = { 0x80, 0x80,
                offset, offset >> 8, offset >> 16, offset >> 24, value };
            cb_append(&code, text, sizeof(text));
        }
    }
    else  /* value < 0 */
    {
        if (offset == 0)
        {
            /* subb <-value>, (%rax) */
            char text[] = { 0x80, 0x28, -value };
            cb_append(&code, text, sizeof(text));
        }
        else
        if (offset >= -128 && offset < 128)
        {   /* subb $<value>, <offset>(%rax) */
            const char text[] = { 0x80, 0x68, offset, -value };
            cb_append(&code, text, sizeof(text));
        }
        else
        {   /* subb $<value>, <offset>(%rax) */
            const char text[] = { 0x80, 0xa8,
                offset, offset >> 8, offset >> 16, offset >> 24, -value };
            cb_append(&code, text, sizeof(text));
        }
    }
    if (wrap_check) check_wrap(offset == 0, value > -256 && value < 256);
}

/* Generate special-case loop code when a loop contains only a single AddMove
   node and nothing else, does not (effectively) move the tape head, and the
   value of the tape head is decremented or incremented by one every iteration.
   In this case, we can eliminate the loop entirely and generate static code
   that adds a constant multiple of the current cell to the affected cells. */
static int gen_special_loop_code(AstNode *child)
{
    int pos, num_bits = 0, zero_check = 0;

    if (child == NULL || child->next != NULL ||   /* must be a single node */
        child->type != OP_ADD_MOVE ||             /* must be Add+Move node */
        child->value != 0) return 0;        /* must not move the tape head */

    /* Verify current cell changes by exactly 1 every step: */
    if (child->add[0] != -1 && child->add[0] != 1) return 0;

    child->code.begin = code.size;

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
        static const char text[] = {
            LONGPREFIX 0x0f, 0xb6, 0x08 };  /* movzbq (%rax), %rcx */
        cb_append(&code, text, sizeof(text));

        /* If we don't know that the current cell is nonzero, then we must do a zero-check here, to
           avoid writing outside tape bounds in a loop which isn't actually executed. */
        if (cell_value != 1) zero_check = code.size;

        for (bit = 0; bit < num_bits; ++bit)
        {
            if (bit > 0)
            {
                static const char text[] = {
                    LONGPREFIX 0x01, 0xc9 };   /* add %rcx, %rcx */
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
                    else
                    {
                        continue;
                    }
                    if (wrap_check) check_wrap(0, 1);
                }
            }
        }
    }

    /* Finally, clear current cell: */
    {
        /* Alternatively, I could use andb $0, (%rax) to achieve the same result while updating the
           zero-flag which may allow me to generate less code. However, this is a slightly less efficient
           operation, and it is unlikely this instruction will be followed by a test anyway. */
        char text[] = { 0xc6, 0x00, 0x00 }; /* movb $0, (%rax) */
        cb_append(&code, text, sizeof(text));
    }

    if (zero_check > 0)
    {   /* Possible FIXME: the main purpose of this check is to prevent writing outside of tape bounds
           when the value to be copied is zero (see tests/bug-1.b for an example where this matters).
           We could omit the check here and handle that case in the SIGSEGV signal handler instead, but
           that's a bit messy. We need benchmarks to determine if it's worth optimizing. */
        int dist = code.size - zero_check;
        assert(dist > 0);
        if (dist < 128)
        {   /* Test and jump near */
            const char text[] = {
                0x84, 0xc9,    /* test %cl, %cl */
                0x74, dist };  /* jz <dist> */
            cb_insert(&code, text, sizeof(text), zero_check);
        }
        else
        {   /* Test and jump far */
            const char text[] = {
                0x84, 0xc9,  /* test %cl, %cl */
                0x0f, 0x84,  /* jz .. */
                dist >> 0, dist >> 8, dist >> 16, dist >> 24 };
            cb_insert(&code, text, sizeof(text), zero_check);
        }
    }

    child->code.end = code.size;

    cell_value = 0;
    zf_valid   = 0;

    return 1;
}

static void gen_code(AstNode *node)
{
    for ( ; node != NULL; node = node->next)
    {
        node->code.begin = code.size;
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
                gen_add(0, node->value);
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
                    check_head();
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
                    gen_add(pos, node->add[pos]);
                }

                gen_move(node->value);

                /* Do addition to current head position last, so we benefit
                   from having an up-to-date zero flag: */
                if (node->add[node->value] != 0)
                {
                    gen_add(0, node->add[node->value]);
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
        node->code.end = code.size;
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
        0x5d,                   /* popq %rbp */
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
    check_head();
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
    struct sigaction sigact;

    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = &signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGSEGV, &sigact, &oldact_sigsegv);
    sigaction(SIGINT, &sigact, &oldact_sigint);
    sigaction(SIGTERM, &sigact, &oldact_sigterm);
    sigaction(SIGVTALRM, &sigact, &oldact_sigvtalrm);
    pagesize = getpagesize();
    cb_create(&code);
    assert(CB_COUNT < 32);
    max_tape_size = 0;
    eof_value = -1;
    vm_alloc(0);
}

void vm_load(AstNode *ast)
{
    cb_truncate(&code);
    program = ast;
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

void vm_set_profiling(int enable)
{
    assert(enable == 0 || enable == 1);
    if (!profile && enable)
    {
        assert(program != NULL);
        profile = calloc(code.size + 1, sizeof(size_t));
        assert(profile != NULL);
    }
    else
    if (profile && !enable)
    {
        free(profile);
        profile = NULL;
    }
}

void vm_exec(void)
{
    Cell *head = tape;

    interrupted = 0;
    setitimer(ITIMER_VIRTUAL, profile ? &timer_on : &timer_off, NULL);
    ((Cell *(*)(Cell*, VM_Callback))code.data)(head, &vm_callback);
    setitimer(ITIMER_VIRTUAL, &timer_off, NULL);
}

void vm_fini(void)
{
    sigaction(SIGSEGV, &oldact_sigsegv, NULL);
    sigaction(SIGINT, &oldact_sigint, NULL);
    sigaction(SIGTERM, &oldact_sigterm, NULL);
    sigaction(SIGVTALRM, &oldact_sigvtalrm, NULL);
    vm_set_profiling(0);
    vm_free();
    cb_destroy(&code);
    program = NULL;
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

size_t *vm_get_profile(size_t *size)
{
    if (size) *size = code.size;
    return profile;
}
