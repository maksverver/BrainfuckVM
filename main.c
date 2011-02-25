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

static const char   *arg_source_path = "-";
static const char   *arg_source      = NULL;
static const char   *arg_input_path  = NULL;
static const char   *arg_output_path = NULL;
static int          arg_wrap_check   = 0;
static int          arg_optimize     = 0;
static int          arg_debug        = -1;
static int          arg_print_code   = 0;
static int          arg_print_tree   = 0;
static const char   *arg_object      = NULL;
static size_t       arg_mem_limit    = (size_t)-1;
static int          arg_eof_value    = -1;

static void exit_usage(void)
{
    printf("Brainfuck interpreter usage:\n"
        "\tbfi <options> [<source.bf>]\n\n"
        "Options:\n"
        "\t-O        optimize\n"
        "\t-c <path> dump compiled object to <path>\n"
        "\t-d <char> debug callback character\n"
        "\t-e <code> use code in argument (don't read from file)\n"
        "\t-i <path> read input from file at <path> instead of standard input\n"
        "\t-m <size> tape memory limit (K, M or G suffix recognized)\n"
        "\t-o <path> write output to file at <path> instead of standard output\n"
        "\t-p        print compact code (don't execute)\n"
        "\t-t        print program tree (don't execute)\n"
        "\t-w        break to debugger when value wraps around\n"
        "\t-z <val>  value written to cell when reading fails\n" );
    exit(0);
}

static size_t parse_size(const char *arg)
{
    unsigned long long val;
    char suffix = '\0';
    sscanf(arg, "%Lu%c", &val, &suffix);
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
    while ((c = getopt(argc, argv, "Oc:d::e:i:m:o:ptwz:")) >= 0)
    {
        switch (c)
        {
        case 'O':
            arg_optimize = 1;
            break;

        case 'c':
            arg_object = optarg;
            break;

        case 'd':
            arg_debug = optarg ? optarg[0] : '!';
            break;

        case 'e':
            arg_source = optarg;
            break;

        case 'i':
            arg_input_path = optarg;
            break;

        case 'm':
            arg_mem_limit = parse_size(optarg);
            break;

        case 'o':
            arg_output_path = optarg;
            break;

        case 'p':
            arg_print_code = 1;
            break;

        case 't':
            arg_print_tree = 1;
            break;

        case 'w':
            arg_wrap_check = 1;
            break;

        case 'z':
            arg_eof_value = atoi(optarg)&255;
            break;

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
}

int main(int argc, char *argv[])
{
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
    if (strcmp(arg_source_path, "-") != 0)
    {
        pr = parse_path(arg_source_path, arg_debug);
    }
    else
    {
        pr = parse_file(stdin, arg_debug);
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
    if (arg_wrap_check) vm_set_wrap_check(arg_wrap_check);
    if (arg_optimize) ast = optimize(ast);

    if (arg_print_code || arg_print_tree)
    {
        if (arg_print_code)
        {
            /* Print program back: */
            ast_print(ast, stdout, 80, arg_debug);
        }
        if (arg_print_tree)
        {
            /* Load code, then print annotated AST */
            vm_init();
            vm_load(ast);
            ast_print_tree(ast, stdout);
            vm_fini();
        }
    }
    else
    {
        /* Execute program: */
        vm_init();
        vm_load(ast);
        if (arg_object == NULL)
        {
            FILE *fp_input = stdin, *fp_output = stdout;

            if (arg_mem_limit != (size_t)-1) vm_set_memlimit(arg_mem_limit);
            if (arg_eof_value != -1) vm_set_eof_value(arg_eof_value);

            if (arg_input_path != NULL &&
                (fp_input = fopen(arg_input_path, "r")) == NULL)
            {
                fprintf(stderr, "Could not open input file `%s' "
                                "for reading!\n", arg_input_path);
            }
            else
            if (arg_output_path != NULL &&
                (fp_output = fopen(arg_output_path, "w")) == NULL)
            {
                fprintf(stderr, "Could not open output file `%s' "
                                "for writing!\n", arg_input_path);
            }
            else
            {
                vm_set_input(fp_input);
                vm_set_output(fp_output);
                vm_exec();
            }
            if (fp_input != stdin && fp_input != NULL) fclose(fp_input);
            if (fp_output != stdout && fp_output != NULL) fclose(fp_output);
        }
        else
        {
            FILE *fp = fopen(arg_object, "wb");
            if (fp == NULL)
            {
                fprintf(stderr, "Could not open `%s' for writing!\n", arg_object);
            }
            else
            {
                vm_dump(fp);
                fclose(fp);
            }
        }
        vm_fini();
    }
    ast_free(ast);
    return 0;
}
