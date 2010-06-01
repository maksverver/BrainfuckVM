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

static const char   *arg_path       = "-";
static const char   *arg_code       = NULL;
static int          arg_optimize    = 0;
static int          arg_debug       = -1;
static int          arg_print       = 0;
static const char   *arg_object     = NULL;
static size_t       arg_mem_limit   = (size_t)-1;

static void exit_usage(void)
{
    printf("Brainfuck interpreter usage:\n"
        "\tbfi <options> [<source.bf>]\n\n"
        "Options:\n"
        "\t-O        optimize\n"
        "\t-d <char> debug callback character\n"
        "\t-e <code> use code in argument (don't read from file)\n"
        "\t-m <size> tape memory limit (K, M or G suffix recognized)\n"
        "\t-p        print compact code (don't execute)\n"
        "\t-o <path> dump compiled object to <path>\n");
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
    while ((c = getopt(argc, argv, "Od::e:m:o:p")) >= 0)
    {
        switch (c)
        {
        case 'O':
            arg_optimize = 1;
            break;

        case 'd':
            arg_debug = optarg ? optarg[0] : '!';
            break;

        case 'e':
            arg_code = optarg;
            break;

        case 'm':
            arg_mem_limit = parse_size(optarg);
            break;

        case 'o':
            arg_object = optarg;
            break;

        case 'p':
            arg_print = 1;
            break;

        default:
            fprintf(stderr, "Invalid option: `%c'\n\n", (char)optopt);
            exit_usage();
        }
    }
    if (optind < argc)
    {
        arg_path = argv[optind++];
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
    if (arg_code != NULL) pr = parse_string(arg_code, arg_debug);
    else if (strcmp(arg_path, "-") != 0) pr = parse_path(arg_path, arg_debug);
    else pr = parse_file(stdin, arg_debug);

    /* Display warnings/errors: */
    for (msg = pr->warnings; msg != NULL; msg = msg->next)
    {
        fprintf(stderr, "Warning at line %d column %d: %s!\n",
                        msg->line, msg->column, msg->message);
        ++warnings;
    }
    for (msg = pr->errors; msg != NULL; msg = msg->next)
    {
        fprintf(stderr, "Error at line %d column %d: %s!\n",
                        msg->line, msg->column, msg->message);
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

    if (arg_print)
    {
        /* Print program back: */
        ast_print(ast, stdout, 80, arg_debug);
        ast_free(ast);
    }
    else
    {
        /* Execute program: */
        vm_init();
        vm_load(ast);
        ast_free(ast);
        if (arg_object == NULL)
        {
            if (arg_mem_limit != (size_t)-1) vm_limit_mem(arg_mem_limit);
            vm_exec();
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

    return 0;
}
