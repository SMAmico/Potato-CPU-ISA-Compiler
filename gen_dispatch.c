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

// Current target (set by main.c)
extern char *target_arch;

// Dispatcher functions

void target_init(FILE *fp) {
    gen_x86_64_init(fp);
}

void target_finalize(void) {
    gen_x86_64_finalize();
}

void target_emit_toplevel(Node *v) {
    gen_x86_64_emit_toplevel(v);
}

void target_set_output_file(FILE *fp) {
    gen_x86_64_set_output_file(fp);
}
