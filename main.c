// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#if defined(__has_include)
# if __has_include(<sys/wait.h>)
#  include <sys/wait.h>
# endif
#elif defined(__unix__) || defined(__APPLE__)
# include <sys/wait.h>
#endif
#include <unistd.h>
#if defined(_WIN32) || defined(_WIN64)
# include <process.h>
# include <fcntl.h>
# include <io.h>
#endif
#include "8cc.h"
#include "target.h"

static char *infile;
static char *outfile;
static char *asmfile;
static bool dumpast;
static bool cpponly;
static bool dumpasm;
static bool dontlink;
static Buffer *cppdefs;
static Vector *tmpfiles = &EMPTY_VECTOR;
char *target_arch = "x86-64";  // Default target: x86-64 or "ex-isa"

static void usage(int exitcode) {
    fprintf(exitcode ? stderr : stdout,
            "Usage: 8cc [ -E ][ -a ] [ -h ] <file>\n\n"
            "\n"
            "  -I<path>          add to include path\n"
            "  -E                print preprocessed source code\n"
            "  -D name           Predefine name as a macro\n"
            "  -D name=def\n"
            "  -S                Stop before assembly (default)\n"
            "  -c                Do not run linker (default)\n"
            "  -U name           Undefine name\n"
            "  -fdump-ast        print AST\n"
            "  -fdump-stack      Print stacktrace\n"
            "  -fno-dump-source  Do not emit source code as assembly comment\n"
            "  -o filename       Output to the specified file\n"
            "  -g                Do nothing at this moment\n"
            "  -Wall             Enable all warnings\n"
            "  -Werror           Make all warnings into errors\n"
            "  -O<number>        Does nothing at this moment\n"
            "  -m64              Output 64-bit code (default)\n"
            "  -w                Disable all warnings\n"
            "  -h                print this help\n"
            "\n"
            "One of -a, -c, -E or -S must be specified.\n\n");
    exit(exitcode);
}

static void delete_temp_files() {
    for (int i = 0; i < vec_len(tmpfiles); i++)
        unlink(vec_get(tmpfiles, i));
}

static char *base(char *path) {
    return basename(strdup(path));
}

static char *replace_suffix(char *filename, char suffix) {
    char *r = format("%s", filename);
    char *p = r + strlen(r) - 1;
    if (*p != 'c')
        error("filename suffix is not .c");
    *p = suffix;
    return r;
}

static FILE *open_asmfile() {
    if (dumpasm) {
        asmfile = outfile ? outfile : replace_suffix(base(infile), 's');
    } else {
        asmfile = format("/tmp/8ccXXXXXX.s");
        int fd = mkstemp(asmfile);
        if (fd < 0) {
            perror("mkstemp");
        } else {
            close(fd);
        }
        vec_push(tmpfiles, asmfile);
    }
    if (!strcmp(asmfile, "-"))
        return stdout;
    FILE *fp = fopen(asmfile, "w");
    if (!fp)
        perror("fopen");
    return fp;
}

static void parse_warnings_arg(char *s) {
    if (!strcmp(s, "error"))
        warning_is_error = true;
    else if (strcmp(s, "all"))
        error("unknown -W option: %s", s);
}

static void parse_f_arg(char *s) {
    if (!strcmp(s, "dump-ast"))
        dumpast = true;
    else if (!strcmp(s, "dump-stack"))
        dumpstack = true;
    else if (!strcmp(s, "no-dump-source"))
        dumpsource = false;
    else
        usage(1);
}

static void parse_m_arg(char *s) {
    if (!strcmp(s, "64")) {
        target_arch = "x86-64";
    } else if (!strcmp(s, "ex-isa")) {
        target_arch = "ex-isa";
    } else {
        error("Unknown -m target: %s (supported: 64, ex-isa)", s);
    }
}

static void parseopt(int argc, char **argv) {
    cppdefs = make_buffer();
    for (;;) {
        int opt = getopt(argc, argv, "I:ED:O:SU:W:acd:f:gm:o:hw");
        if (opt == -1)
            break;
        switch (opt) {
        case 'I': add_include_path(optarg); break;
        case 'E': cpponly = true; break;
        case 'D': {
            char *p = strchr(optarg, '=');
            if (p)
                *p = ' ';
            buf_printf(cppdefs, "#define %s\n", optarg);
            break;
        }
        case 'O': break;
        case 'S': dumpasm = true; break;
        case 'U':
            buf_printf(cppdefs, "#undef %s\n", optarg);
            break;
        case 'W': parse_warnings_arg(optarg); break;
        case 'c': dontlink = true; break;
        case 'f': parse_f_arg(optarg); break;
        case 'm': parse_m_arg(optarg); break;
        case 'g': break;
        case 'o': outfile = optarg; break;
        case 'w': enable_warning = false; break;
        case 'h':
            usage(0);
        default:
            usage(1);
        }
    }
    if (optind != argc - 1)
        usage(1);

    if (!dumpast && !cpponly && !dumpasm && !dontlink)
        error("One of -a, -c, -E or -S must be specified");
    infile = argv[optind];
}

char *get_base_file() {
    return infile;
}

static void preprocess() {
    for (;;) {
        Token *tok = read_token();
        if (tok->kind == TEOF)
            break;
        if (tok->bol)
            printf("\n");
        if (tok->space)
            printf(" ");
        printf("%s", tok2s(tok));
    }
    printf("\n");
    exit(0);
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (atexit(delete_temp_files))
        perror("atexit");
    parseopt(argc, argv);
    lex_init(infile);
    cpp_init();
    parse_init();
    
    FILE *asmfp = open_asmfile();
    set_output_file(asmfp);
    
    // Add target-specific preprocessor defines
    if (!strcmp(target_arch, "ex-isa")) {
        buf_printf(cppdefs, "#define __EX_ISA__ 1\n");
    }
    
    if (buf_len(cppdefs) > 0)
        read_from_string(buf_body(cppdefs));

    if (cpponly)
        preprocess();

    // Initialize the target backend
    target_init(asmfp);
    
    Vector *toplevels = read_toplevels();
    for (int i = 0; i < vec_len(toplevels); i++) {
        Node *v = vec_get(toplevels, i);
        if (dumpast)
            printf("%s", node2s(v));
        else
            target_emit_toplevel(v);
    }

    // Finalize the target backend
    target_finalize();
    
    close_output_file();

    if (!dumpast && !dumpasm) {
        if (!outfile)
            outfile = replace_suffix(base(infile), 'o');
        
        // Skip GNU assembler for EX_ISA target
        if (!strcmp(target_arch, "ex-isa")) {
            // For EX_ISA, we already emitted EX_ISA assembly
            // In a full implementation, we'd call a separate EX_ISA assembler
            // For now, just skip the GNU as step
            fprintf(stderr, "Note: EX_ISA target does not use GNU assembler\n");
        } else {
#if defined(_WIN32) || defined(_WIN64)
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "as -o \"%s\" -c \"%s\"", outfile, asmfile);
            int status = system(cmd);
            if (status != 0)
                error("as failed");
#else
            pid_t pid = fork();
            if (pid < 0) perror("fork");
            if (pid == 0) {
                execlp("as", "as", "-o", outfile, "-c", asmfile, (char *)NULL);
                perror("execl failed");
            }
            int status;
            waitpid(pid, &status, 0);
            if (status < 0)
                error("as failed");
#endif
        }
    }
    return 0;
}
