#include "ast.h"
#include "ast-printer.h"
#include "parser.h"
#include "optimizer.h"
#include "vm.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

static int          arg_debug        = -1;
static const char   *arg_source      = NULL;
static int          arg_separator    = -1;
static int          arg_optimize     = 0;
static int          arg_wrap_check   = 0;
static int          arg_compile_only = 0;
static int          arg_print_code   = 0;
static int          arg_print_tree   = 0;
static const char   *arg_input_path  = NULL;
static const char   *arg_output_path = NULL;
static size_t       arg_mem_limit    = (size_t)-1;
static int          arg_eof_value    = -1;
static const char   *arg_source_path = "-";

static void exit_usage(void)
{
    printf(
"Brainfuck interpreter usage:\n"
"   bfi <options> [<source.bf>]\n"
"\n"
"Source code processing option:\n"
"    -e <code>  source code (don't read from file)\n"
"    -d <char>  debug breakpoint character (default argument: '#')\n"
"    -s <char>  separator of source code and input (default argument: '!')\n"
"Code generation options:\n"
"    -O         optimize\n"
"    -w         wraparound detection\n"
"Alternative output options:\n"
"    -c         compile object file (don't execute)\n"
"    -p         print compact code (don't execute)\n"
"    -t         print program tree (don't execute)\n"
"Execution options:\n"
"    -i <path>  read input from file at <path> instead of standard input\n"
"    -o <path>  write output to file at <path> instead of standard output\n"
"    -m <size>  tape memory limit (K, M or G suffix recognized)\n"
"    -z <byte>  value stored when reading fails (default: none)\n" );
    exit(0);
}

static size_t parse_size(const char *arg)
{
    unsigned long long val;
    char suffix = '\0';
    sscanf(arg, "%llu%c", &val, &suffix);
    switch (suffix)
    {
    case 'G': case 'g': val *= 1024; /* falls through */
    case 'M': case 'm': val *= 1024; /* falls through */
    case 'K': case 'k': val *= 1024; /* falls through */
    }
    return (size_t)val;
}

static void parse_args(int argc, char *argv[])
{
    int c;
    while ((c = getopt(argc, argv, "d::e:s::Owcpti:o:m:z:")) >= 0)
    {
        switch (c)
        {
        case 'd': arg_debug = (optarg ? optarg[0] : '#')&255; break;
        case 'e': arg_source = optarg; break;
        case 's': arg_separator = (optarg ? optarg[0] : '!')&255; break;
        case 'O': arg_optimize = 1; break;
        case 'w': arg_wrap_check = 1; break;
        case 'c': arg_compile_only = 1; break;
        case 'p': arg_print_code = 1; break;
        case 't': arg_print_tree = 1; break;
        case 'i': arg_input_path = optarg; break;
        case 'o': arg_output_path = optarg; break;
        case 'm': arg_mem_limit = parse_size(optarg); break;
        case 'z': arg_eof_value = atoi(optarg)&255; break;
        case '?':
            /* getopt has already printed an error message */
            putc('\n', stderr);
            exit_usage();

        default:
            assert(0);  /* unhandled option. should not happen! */
        }
    }
    if (arg_source == NULL && optind < argc)
    {
        arg_source_path = argv[optind++];
    }
    if (optind < argc)
    {
        fprintf(stderr, "Too many command line arguments!\n\n");
        exit_usage();
    }
    if (arg_source && arg_separator >= 0)
    {
        fprintf(stderr, "Cannot specify both -e and -s!\n\n");
        exit_usage();
    }
}

int main(int argc, char *argv[])
{
    FILE *fp_source = NULL;
    AstNode *ast;
    ParseResult *pr;
    ParseMessage *msg;
    int warnings = 0, errors = 0;

    parse_args(argc, argv);

    /* Parse input program: */
    if (arg_source != NULL)
    {
        pr = parse_string(arg_source, arg_debug);
    }
    else
    {
        if (strcmp(arg_source_path, "-") != 0)
        {
            fp_source = fopen(arg_source_path, "rb");
            if (fp_source == NULL)
            {
                fprintf(stderr, "Could not open source file `%s' "
                                "for reading!\n", arg_source_path);
                exit(1);
            }
        }
        else
        {
            fp_source = stdin;
        }
        pr = parse_file(fp_source, arg_debug, arg_separator);
        if (arg_input_path == NULL && arg_separator >= 0)
        {
            if (getc(fp_source) != arg_separator)
            {
                fprintf(stderr, "Warning: missing separator at end of input!\n");
            }
        }
        else
        {
            if (fp_source != stdin) fclose(fp_source);
            fp_source = NULL;
        }
    }

    /* Display warnings/errors: */
    for (msg = pr->warnings; msg != NULL; msg = msg->next)
    {
        fprintf(stderr, "Warning at line %d column %d: %s!\n",
            SRCLOC_LINE(msg->origin), SRCLOC_COLUMN(msg->origin), msg->message);
        ++warnings;
    }
    for (msg = pr->errors; msg != NULL; msg = msg->next)
    {
        fprintf(stderr, "Error at line %d column %d: %s!\n",
            SRCLOC_LINE(msg->origin), SRCLOC_COLUMN(msg->origin), msg->message);
        ++errors;
    }
    if (warnings + errors > 0)
    {
        fprintf(stderr, "%d warnings, %d errors in total.\n", warnings, errors);
    }

    /* Free parse result: */
    ast = pr->ast;
    pr->ast = NULL;
    parse_free_result(pr);

    /* Optimize program (if requested): */
    if (arg_optimize) ast = optimize(ast);


    /* Initialize VM */
    vm_init();
    if (arg_wrap_check) vm_set_wrap_check(arg_wrap_check);

    if (arg_print_code)
    {
        /* Print program back: */
        ast_print(ast, stdout, 80, arg_debug);
    }

    if (arg_print_tree)
    {
        /* Load code, then print annotated AST */
        vm_load(ast);
        ast_print_tree(ast, stdout);
        vm_fini();
    }

    if (arg_compile_only)
    {
        const char *path = arg_output_path != NULL ? arg_output_path : "a.out";
        FILE *fp = fopen(path, "wb");

        if (fp == NULL)
        {
            fprintf(stderr, "Could not open object file `%s'!\n", path);
        }
        else
        {
            vm_load(ast);
            vm_dump(fp);
            fclose(fp);
        }
    }

    if (!arg_print_code && !arg_print_tree && !arg_compile_only)
    {
        FILE *fp_input = stdin, *fp_output = stdout;

        if (fp_source != NULL)
        {
            fp_input = fp_source;
            fp_source = NULL;
        }

        if (arg_input_path != NULL &&
            (fp_input = fopen(arg_input_path, "r")) == NULL)
        {
            fprintf(stderr, "Could not open input file `%s'!\n",
                            arg_input_path);
        }
        else
        if (arg_output_path != NULL &&
            (fp_output = fopen(arg_output_path, "w")) == NULL)
        {
            fprintf(stderr, "Could not open output file `%s'!\n",
                            arg_output_path);
        }
        else
        {
            if (arg_mem_limit != (size_t)-1) vm_set_memlimit(arg_mem_limit);
            if (arg_eof_value != -1) vm_set_eof_value(arg_eof_value);
            vm_load(ast);
            vm_set_input(fp_input);
            vm_set_output(fp_output);
            vm_exec();
        }
        if (fp_input != stdin && fp_input != NULL) fclose(fp_input);
        if (fp_output != stdout && fp_output != NULL) fclose(fp_output);
    }

    if (fp_source != stdin && fp_source != NULL) fclose(fp_source);
    vm_fini();
    ast_free(ast);
    return 0;
}
