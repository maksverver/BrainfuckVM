#include "debugger.h"
#include <stdio.h>

static int cont;

struct Command {
    const char *name;                               /* command name */
    const char *description;                        /* textual description */
    void (*handler)(Cell **head, const char *cmd);  /* handler function */
};

void debug_help(Cell **head, const char *cmd);
void debug_quit(Cell **head, const char *cmd);
void debug_continue(Cell **head, const char *cmd);
void debug_display(Cell **head, const char *cmd);
void debug_head(Cell **head, const char *cmd);
void debug_move(Cell **head, const char *cmd);
void debug_write(Cell **head, const char *cmd);
void debug_add(Cell **head, const char *cmd);
void debug_subtract(Cell **head, const char *cmd);

static struct Command commands[] = {
{ "help", "[<command>]\n"
"\tDisplays usage information for all matching commands. Without an argument,\n"
"\tdisplays information on all commands.\n",
    &debug_help },
{ "quit", "\n"
"\tAborts the currently running program and quits the debugger.\n",
    debug_quit },
{ "continue", "[<N>]\n"
"\tResumes execution until the N'th next breakpoint. Without argument, N\n"
"\tdefaults to 1 (break at the next breakboint). When N is 0, continues\n"
"\texecution indefinitely, never breaking again.\n",
    debug_continue },
{ "display", "[<start> [<length>]]\n"
"\tDisplays tape memory starting from `start' and printing `length' bytes\n"
"\tin total. If `length' is not given, prints a single row of data. If\n"
"\t`start' is not given, selects the starting address such that the current\n"
"\ttape head is in the middle of the displayed data.\n",
    debug_display },
{ "head", "[<position>]\n"
"\tWithout an argument, displays the current (zero-based) position of the\n"
"\ttape head. With an argument, places the head at the specified position.\n",
    debug_head },
{ "move", "<distance>\n"
"\tMoves the tape head by the given distance, which may be positive or\n"
"\tnegative.\n",
    debug_move },
{ "write", "<value> [<offset>]\n"
"\tWrite a value to the cell at an offset relative to the tape head.\n"
"\tIf no offset is provided, it is assumed to be 0.\n",
    debug_write },
{ "add", "<value> [<offset>]\n"
"\tAdd a value to the cell at an offset relative to the tape head.\n"
"\tIf no offset is provided, it is assumed to be 0.\n",
    debug_add },
{ "subtract", "<value> [<offset>]\n"
"\tSubtract a value from the cell at an offset relative to the tape head.\n"
"\tIf no offset is provided, it is assumed to be 0.\n",
    debug_subtract },
{ NULL, NULL, NULL } };

/* TODO:
    - functions to display/manipulate the current code position.
    - function to write to memory at an absolute address.
*/

static Cell *extend_tape(Cell **head, long long new_pos)
{
    Cell *tape;
    size_t size;

    tape = vm_memory(&size);
    while (new_pos >= 0 && (unsigned long long)new_pos >= size)
    {
        vm_expand(head);
        tape = vm_memory(&size);
    }
    return tape;
}

static void set_head_pos(Cell **head, long long new_pos)
{
    Cell *tape = extend_tape(head, new_pos);
    if (new_pos < 0) new_pos = 0;
    *head = tape + new_pos;
    fprintf(stderr, "%lld\n", (long long)(*head - tape));
}

static int command_match(const char *name, const char *str)
{
    while ((*str & 0xff) <= 32) ++str;
    while (*str)
    {
        if (*str == '\0' || (*str & 0xff) <= 32) return 1;
        if (*str++ != *name++) return 0;
    }
    return 1;
}

void debug_help(Cell **head, const char *cmd)
{
    struct Command *c;
    char prefix[100];

    if (sscanf(cmd, "%*s %99s", prefix) < 1)
    {
        fprintf(stderr,
"The debugger supports the following commands. Each command can be abbreviated\n"
"to a unique prefix of the command (e.g. `c' instead of `continue').\n");
        *prefix = '\0';
    }

    for (c = commands; c->name != NULL; ++c)
    {
        if (*prefix && !command_match(c->name, prefix)) continue;
        fprintf(stderr, "\n%s %s", c->name, c->description);
        if (c->handler == NULL) fprintf(stderr, "\t(Not implemented yet!)\n");
    }

    (void)head, (void)cmd;  /* unused */
}

void debug_quit(Cell **head, const char *cmd)
{
    exit(0);
    (void)head, (void)cmd;  /* unused */
}

void debug_continue(Cell **head, const char *cmd)
{
    int N;
    if (sscanf(cmd, "%*s %d", &N) < 1 || N < 0) N = 1;
    cont = (N == 0) ? -1 : N;
    (void)head;  /* unused */
}

void debug_display(Cell **head, const char *cmd)
{
    long long arg_start, arg_length;
    size_t size, start, length, n;
    Cell *tape;

    tape = vm_memory(&size);

    /* Parse start and length: */
    arg_start  = (long long)(*head - tape) - 6;
    arg_length = -1;
    sscanf(cmd, "%*s %lld %lld", &arg_start, &arg_length);
    start  = arg_start > 0 ? (size_t)arg_start : (size_t)0;
    length = arg_length > 0 ? (size_t)arg_length : 14;
    if (length > size) length = size;

    /* Display selected range of data: */
    for (n = 0; n < length; ++n)
    {
        if (n%14 == 0)
        {
            if (n > 0) putc('\n', stderr);
            fprintf(stderr, "%8lld: ", (long long)(start + n));
        }
        fprintf(stderr,
            (tape + start + n == *head) ? "[%3d]" : " %3d ",
            (start + n < size) ? tape[start + n] : 0 );
    }
    putc('\n', stderr);
}

void debug_head(Cell **head, const char *cmd)
{
    size_t size;
    Cell *tape;
    long long new_pos;

    tape = vm_memory(&size);
    if (sscanf(cmd, "%*s %lld", &new_pos) < 1) new_pos = *head - tape;
    set_head_pos(head, new_pos);
}

void debug_move(Cell **head, const char *cmd)
{
    size_t size;
    Cell *tape;
    long long dist;

    if (sscanf(cmd, "%*s %lld", &dist) < 1)
    {
        fprintf(stderr, "Too few arguments for `move' command!\n");
    }
    else
    {
        tape = vm_memory(&size);
        set_head_pos(head, (*head - tape) + dist);
    }
}

static void change_value(Cell **head, const char *cmd,
                         const char *name, int a, int b)
{
    int args, value;
    long long pos, offset;
    Cell *tape = vm_memory(NULL);

    args = sscanf(cmd, "%*s %d %lld", &value, &offset);
    if (args < 1)
    {
        fprintf(stderr, "Too few arguments for %s' command!\n", name);
    }
    else
    {
        if (args < 2) offset = 0;
        pos = (long long)(*head - tape) + offset;
        if (pos < 0)
        {
            fprintf(stderr, "Target position (%lld) out of bounds!\n", pos);
        }
        else
        {
            tape = extend_tape(head, pos);
            tape[pos] = a*tape[pos] + b*(Cell)value;
        }
    }
}

void debug_write(Cell **head, const char *cmd)
{
    return change_value(head, cmd, "write", 0, 1);
}

void debug_add(Cell **head, const char *cmd)
{
    return change_value(head, cmd, "add", 1, 1);
}

void debug_subtract(Cell **head, const char *cmd)
{
    return change_value(head, cmd, "subtract", 1, -1);
}

void debug_break(Cell **head)
{
    char line[1024];

    fflush(stdout);
    while (cont == 0)
    {
        struct Command *c, *matched = NULL;
        int matches = 0;

        fprintf(stderr, "(debug) ");
        fflush(stderr);
        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            putc('\n', stderr);
            exit(0);
        }
        for (c = commands; c->name != NULL; ++c)
        {
            if (command_match(c->name, line))
            {
                matched = c;
                ++matches;
            }
        }
        if (matches != 1 || matched->handler == NULL)
        {
            fprintf(stderr,
                "Command %s. Type `help' for a list of supported commands.\n",
                (matches == 0) ? "not recognized" :
                (matches > 1) ? "is ambiguous" : "is not implemented" );
        }
        else
        {
            matched->handler(head, line);
        }
    }
    if (cont > 0) --cont;
}
