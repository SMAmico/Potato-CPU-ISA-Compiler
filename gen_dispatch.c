// Target backend dispatcher
// Routes code generation to the appropriate backend based on -m flag

#include <stdio.h>
#include <string.h>
#include "8cc.h"
#include "target.h"

// Extern target implementations
extern void gen_x86_64_init(FILE *fp);
extern void gen_x86_64_finalize(void);
extern void gen_x86_64_emit_toplevel(Node *v);
extern void gen_x86_64_set_output_file(FILE *fp);

extern void gen_ex_isa_init(FILE *fp);
extern void gen_ex_isa_finalize(void);
extern void gen_ex_isa_emit_toplevel(Node *v);
extern void gen_ex_isa_set_output_file(FILE *fp);

// Current target (set by main.c)
extern char *target_arch;

// Dispatcher functions

void target_init(FILE *fp) {
    if (!strcmp(target_arch, "ex-isa")) {
        gen_ex_isa_init(fp);
    } else {
        gen_x86_64_init(fp);
    }
}

void target_finalize(void) {
    if (!strcmp(target_arch, "ex-isa")) {
        gen_ex_isa_finalize();
    } else {
        gen_x86_64_finalize();
    }
}

void target_emit_toplevel(Node *v) {
    if (!strcmp(target_arch, "ex-isa")) {
        gen_ex_isa_emit_toplevel(v);
    } else {
        gen_x86_64_emit_toplevel(v);
    }
}

void target_set_output_file(FILE *fp) {
    if (!strcmp(target_arch, "ex-isa")) {
        gen_ex_isa_set_output_file(fp);
    } else {
        gen_x86_64_set_output_file(fp);
    }
}
