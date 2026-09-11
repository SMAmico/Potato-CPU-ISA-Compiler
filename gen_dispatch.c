// Target backend dispatcher
// Routes code generation to the appropriate backend based on -m flag

#include <stdio.h>
#include <string.h>
#include "8cc.h"
#include "target.h"

// Extern target implementations
#ifdef DEFAULT_TARGET_EX_ISA
extern void gen_ex_isa_init(FILE *fp);
extern void gen_ex_isa_finalize(void);
extern void gen_ex_isa_emit_toplevel(Node *v);
extern void gen_ex_isa_set_output_file(FILE *fp);
#else
extern void gen_x86_64_init(FILE *fp);
extern void gen_x86_64_finalize(void);
extern void gen_x86_64_emit_toplevel(Node *v);
extern void gen_x86_64_set_output_file(FILE *fp);
#endif

// Current target (set by main.c)
extern char *target_arch;

// Dispatcher functions

void target_init(FILE *fp) {
#ifdef DEFAULT_TARGET_EX_ISA
    gen_ex_isa_init(fp);
#else
    gen_x86_64_init(fp);
#endif
}

void target_finalize(void) {
#ifdef DEFAULT_TARGET_EX_ISA
    gen_ex_isa_finalize();
#else
    gen_x86_64_finalize();
#endif
}

void target_emit_toplevel(Node *v) {
#ifdef DEFAULT_TARGET_EX_ISA
    gen_ex_isa_emit_toplevel(v);
#else
    gen_x86_64_emit_toplevel(v);
#endif
}

void target_set_output_file(FILE *fp) {
#ifdef DEFAULT_TARGET_EX_ISA
    gen_ex_isa_set_output_file(fp);
#else
    gen_x86_64_set_output_file(fp);
#endif
}
