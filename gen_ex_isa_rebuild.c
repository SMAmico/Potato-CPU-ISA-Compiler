// EX_ISA backend for 8cc compiler
// Target: 4-bit opcodes, 16 registers, 16-bit words, 256-byte address space
//
// Memory layout:
//   0x00-0xFF: code (256 bytes ROM)
//   0x00-0xBF: global data (64 bytes)
//   0xC0-0xFF: stack (64 bytes, grows downward from 0xFF)

/*
    Simple two-pass assembler for the project's EX_ISA. all emitted C code should compile into this format.

    Usage: assembler-EX_ISA <input.asm> <output.txt>

    Assembly syntax (whitespace and commas separate tokens):
      - Labels: `label:` at start of a line
      - Comments: start with `;`, `//`, or `#`
      - Registers: R1 .. R13 (case-insensitive) or numeric 0..13 with R14 as TMP, R15 as PC, and R0 as zero register
      ; Assembly formatting instructions:
      - Use .text for instructions and instruction labels.
      - Use .data for data directives and data labels.
      - Labels end with ':' and may appear on their own line.
      - Tokens are separated by whitespace and/or commas.
      - Comments may start with ';', '//' or '#'.
      - Registers are R1..R13 (case-insensitive).
      - Control-flow labels (JMP/JNZ/JLT label form) must be .text labels.
      - Memory-address labels (STR/LDR label form) must be .data labels.

    Instruction formats implemented :

      STR rA, rB/LABEL, soff (store RF[rA] -> D[RF[rB] + soff]) -> 0001 raaa rbbb soff  pseudo-ins variant accepts -8..+7 word offset
      LDR rA, rB/LABEL, soff (load D[RF[rB] + soff] -> RF[rA])  -> 0010 raaa rbbb soff  pseudo-ins variant accepts -8..+7 word offset

      ADD rA, rB, rC (rA = rB + rC)     -> 0011 raaa rbbb rccc
      SUB rA, rB, rC (rA = rB - rC)     -> 0100 raaa rbbb rccc
      HLT                               -> 0101 0000 0000 0000

      MOVI rA, rB, hex (rA = rB | hex)  -> 0110 raaa dddddddd     (ORs the immediate value into the selected register, using pseudoins for >8 bits)
      OR  rA, rB, rC   (rA = rB | rC)   -> 0111 raaa rbbb rccc
      AND rA, rB, rC   (rA = rB & rC)   -> 1000 raaa rbbb rccc

      JMP addr/LABEL   (PC = addr)      -> 1001 0000 bbbbbbbb    (absolute 8-bit instruction addr)
      JNZ addr/LABEL, r (PC = addr if r != 0)  -> 1010 bbbbbbbb rrrr    (absolute addr, test reg)
      JLT rA, rB, offset (PC = PC + offset if rA < rB) -> 1011 raaa rbbb bbbb    (4-bit signed offset relative to next instr)

      SHL rA, rB, rC   (rA = rB << rC)  -> 1100 raaa shft rccc     (added instructions for extra ALU ops)
      MULT rA, rB, rC  (rA = rB * rC)   -> 1101 raaa rbbb rccc    
      SHR rA, rB, rC   (rA = rB >> rC)  -> 0000 raaa shft rccc

      NOP                               -> 1000 0000 0000 0000   (AND R0 with R0 into R0, effectively a NOP)
      MOV rA, rB       (rA = rB)  -> 1000 raaa rbbb rccc    (AND RA with RA into RB, effectively moving) 
      XOR rA, rB, rC   (rA = rB ^ rC)  -> pseudo-ins


    The assembler supports labels for addresses and computes relative offsets
    for `JLT` as: offset = target_address - (current_address + 1). Offset must fit
    in signed 4-bit (-8..+7).
*/

//DEFINES: aliases for all instructions in the ISA
#define ins_shr 0x0
#define ins_str 0x1
#define ins_ldr 0x2
#define ins_add 0x3
#define ins_sub 0x4
#define ins_hlt 0x5
#define ins_movi 0x6
#define ins_or 0x7
#define ins_and 0x8
#define ins_jmp 0x9
#define ins_jnz 0xA
#define ins_jlt 0xB
#define ins_shl 0xC
#define ins_mult 0xD

//dedicated registers for stack and frame pointers
#define pc 15       //program counter register
#define asm_tmp 14  //temp register for assembly to machine code interpretation
#define tmp 13      //temp register for extended c interpretation
#define sp 12       //stack pointer register
#define fp 11       //frame pointer register
#define gb 10       //global base register
#define zero 0

//x86 registers hardcoded to match EX_ISA registers
#define rax 1   //x86 accumulator register equivalent
#define rbx 2   //x86 base register equivalent
#define rcx 3   //x86 counter register equivalent
#define rdx 4   //x86 data register equivalent
#define rsi 5   //x86 source index register equivalent
#define rdi 6   //x86 destination index register equivalent
#define rbp fp  //x86 base pointer register equivalent
#define rsp sp  //x86 stack pointer register equivalent

// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "8cc.h"

bool dumpstack = false;
bool dumpsource = true;

//x86 register names: do we need them?
static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char *SREGS[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *MREGS[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
//tab length
static int TAB = 8;
//empty vectore for compiled functions
static Vector *functions = &EMPTY_VECTOR;
//location of stack
static int stackpos;
//number of global variables
static int numgp;
//number of floating point variables
static int numfp;
//output file path
static FILE *outputfp;
//map of compiled source files and lines
static Map *source_files = &EMPTY_MAP;
static Map *source_lines = &EMPTY_MAP;
//pointer to last line read
static char *last_loc = "";

//forward declarations of expressions and addresses
static void emit_addr(Node *node);
static void emit_expr(Node *node);
static void emit_decl_init(Vector *inits, int off, int totalsize);
static void do_emit_data(Vector *inits, int size, int off, int depth);
static void emit_data(Node *v, int off, int depth);

///register byte-size: 1 byte registers x16
#define REGAREA_SIZE 16

//map emit to emitf for file output
#define emit(...)        emitf(__LINE__, "\t" __VA_ARGS__)
#define emit_noindent(...)  emitf(__LINE__, __VA_ARGS__)

//gnu c save call definition
#ifdef __GNUC__
#define SAVE                                                            \
    int save_hook __attribute__((unused, cleanup(pop_function)));       \
    if (dumpstack)                                                      \
        vec_push(functions, (void *)__func__);

static void pop_function(void *ignore) {
    if (dumpstack)
        vec_pop(functions);
}
#else
#define SAVE
#endif

///make a buffer of addresses for use
static char *get_caller_list() {
    Buffer *b = make_buffer();
    for (int i = 0; i < vec_len(functions); i++) {
        if (i > 0)
            buf_printf(b, " -> ");
        buf_printf(b, "%s", vec_get(functions, i));
    }
    buf_write(b, '\0');
    return buf_body(b);
}

/// @brief set the output file for the program
/// @param fp 
void set_output_file(FILE *fp) {
    outputfp = fp;
}

/// @brief close the output file when the program is done
void close_output_file() {
    fclose(outputfp);
}

/// @brief replace # with %% to make vprintf work
/// @param line 
/// @param fmt 
/// @param  
static void emitf(int line, char *fmt, ...) {
    // Replace "#" with "%%" so that vfprintf prints out "#" as "%".
    char buf[256];
    int i = 0;
    for (char *p = fmt; *p; p++) {
        assert(i < sizeof(buf) - 3);
        if (*p == '#') {
            buf[i++] = '%';
            buf[i++] = '%';
        } else {
            buf[i++] = *p;
        }
    }
    buf[i] = '\0';

    va_list args;
    va_start(args, fmt);
    int col = vfprintf(outputfp, buf, args);
    va_end(args);

    if (dumpstack) {
        for (char *p = fmt; *p; p++)
            if (*p == '\t')
                col += TAB - 1;
        int space = (28 - col) > 0 ? (30 - col) : 2;
        fprintf(outputfp, "%*c %s:%d", space, '#', get_caller_list(), line);
    }
    fprintf(outputfp, "\n");
}

/// @brief output one line of instructions
/// @param fmt 
/// @param  
static void emit_nostack(char *fmt, ...) {
    fprintf(outputfp, "\t");
    va_list args;
    va_start(args, fmt);
    vfprintf(outputfp, fmt, args);
    va_end(args);
    fprintf(outputfp, "\n");
}

/// @brief overload: special case for store/load with labels
/// @param op 
/// @param a 
static void emit_asm(int op, int a, string label, int off) {

}

/// @brief overload: emits an assembly instruction bypassing the emitf format
/// @param op 
/// @param a 
static void emit_asm(int op, int a) {
    switch (op) {
    case ins_hlt:
        emit("hlt");
        return;
    case ins_jmp:
        emit("jmp %d", a);
        return;
    default:
        error("Unknown EX_ISA opcode: %d", op);
    }
}

/// @brief overload: emits an assembly instruction bypassing the emitf format
/// @param op 
/// @param a 
/// @param b 
static void emit_asm(int op, int a, int b) {
    switch (op) {
    case ins_str:
        emit("str %d, %d, %d", a, b, 0);
        return;
    case ins_ldr:
        emit("ldr %d, %d, %d", a, b, 0);
        return;
    case ins_hlt:
        emit("hlt");
        return;
    case ins_movi:
        emit("movi %d, %d", a, b);
        return;
    case ins_jmp:
        emit("jmp %d", a);
        return;
    case ins_jnz:
        emit("jnz %d, %d", a, b);
        return;
    default:
        error("Unknown EX_ISA opcode: %d", op);
    }
}

/// @brief emits an assembly instruction bypassing the emitf format
/// @param op 
/// @param a 
/// @param b 
/// @param c 
static void emit_asm(int op, int a, int b, int c) {
    switch (op) {
    case ins_shr:
        emit("shr %d, %d, %d", a, b, c);
        return;
    case ins_str:
        emit("str %d, %d, %d", a, b, c);
        return;
    case ins_ldr:
        emit("ldr %d, %d, %d", a, b, c);
        return;
    case ins_add:
        emit("add %d, %d, %d", a, b, c);
        return;
    case ins_sub:
        emit("sub %d, %d, %d", a, b, c);
        return;
    case ins_hlt:
        emit("hlt");
        return;
    case ins_or:
        emit("or %d, %d, %d", a, b, c);
        return;
    case ins_and:
        emit("and %d, %d, %d", a, b, c);
        return;
    case ins_jlt:
        emit("jlt %d, %d, %d", a, b, c);
        return;
    case ins_shl:
        emit("shl %d, %d, %d", a, b, c);
        return;
    case ins_mult:
        emit("mult %d, %d, %d", a, b, c);
        return;
    default:
        error("Unknown EX_ISA opcode: %d", op);
    }
}

/// @brief convert data size to the matching register type
///        a-type or c-type
/// @param ty 
/// @param r 
/// @return 
static char *get_int_reg(Type *ty, char r) {
    //these registers aren't that important since there's only one length
    assert(r == 'a' || r == 'c');
    switch (ty->size) {
        //16 bits is all we have!
    case 16: return (r == 'a') ? rax : rcx;
    default:
        error("Unknown data size: %s: %d", ty2s(ty), ty->size);
    }
}

/// @brief select the appropriate mov command by bit length (just mov)
/// @param ty 
/// @return 
static char *get_load_inst(Type *ty) {
    switch (ty->size) {
    case 16: return ins_movi;
    default:
        error("Unknown data size: %s: %d", ty2s(ty), ty->size);
    }
}

/// @brief align n data with m offset 
/// @param n 
/// @param m 
/// @return 
static int align(int n, int m) {
    int rem = n % m;
    return (rem == 0) ? n : n - rem + m;
}

/// @brief potato | emits: push an fp register to the stack, update sp manually
/// @param reg 
static void push_xmm(int reg) {
    SAVE;
    //subtract 1 from stack pointer (word address in direct memory mapping)
    emit_asm(ins_movi, tmp, 1, 0);
    emit_asm(ins_sub, sp, tmp, sp); //PROBLEM: sp only contains a register value, isa can only write direct values
                                         //SOLUTION: make str and ldr pull addresses from registers instead of
                                         // direct memory mapping, expanding address space to 16 bit 
                                         //FIXED: this is now fixed in the arch
    //emit("sub $8, #rsp");
    //then store register at stack pointer
    emit_asm(ins_str, reg, sp, 0);
    //emit("movsd #xmm%d, (#rsp)", reg);
    stackpos += 1;
}

/// @brief potato | emits: pop a register from the stack, update sp manually
/// @param reg 
static void pop_xmm(int reg) {
    SAVE;
    emit_asm(ins_ldr, reg, sp, 0);
    //add 1 to stack pointer
    //emit("movsd (#rsp), #xmm%d", reg);
    emit_asm(ins_movi, tmp, 1, 0);
    emit_asm(ins_add, sp, tmp, sp);
    stackpos -= 1;
    assert(stackpos >= 0);
}

// -- 7/17/26 start --

/// @brief potato | emits: push a register to the global stack, update sp
/// @param reg 
static void push(char *reg) {
    SAVE;
    emit("movi %d, %d", tmp, 1);
    emit("sub %d, %d, %d", sp, tmp, sp);
    emit("str %d, %d", reg, sp);
    stackpos += 1; //remember, each instruction is 16 bits, so only 1 word
}

/// @brief potato | emits: pop a register from the global stack, update sp
/// @param reg 
static void pop(char *reg) {
    SAVE;
    emit("ldr %d, %d", reg, sp);
    emit("movi %d, %d", tmp, 1);
    emit("add %d, %d, %d", sp, tmp, sp);
    stackpos -= 1;  //remember, each instruction is 16 bits, so only 1 word
    assert(stackpos >= 0);
}

// -- 7/21/26 start --

/// @brief potato | emits: push a complete structure to the stack
/// @param size 
/// @return 
static int push_struct(int size) {
    SAVE;

    /*
     * The EX_ISA backend is word-addressed (16-bit words), so reserve
     * enough space for the struct rounded up to the nearest word and keep
     * one slot for the preserved rcx value.
     */
    int aligned = align(size, 2);
    int words = aligned / 2;

    emit_asm(ins_movi, tmp, words + 1);
    emit_asm(ins_sub, sp, tmp, sp);
    emit_asm(ins_str, rcx, sp, 0);

    /* Copy the source struct pointer from rax into rcx and then copy the
     * struct contents to the new stack frame one word at a time. */
    emit_asm(ins_add, rcx, rax, zero);
    for (int i = 0; i < words; i++) {
        emit_asm(ins_ldr, tmp, rcx, i);
        emit_asm(ins_str, tmp, sp, i + 1);
    }

    emit_asm(ins_ldr, rcx, sp, 0);
    stackpos += words + 1;
    return aligned;
}

/// @brief potato | emit: trims off last bit of rax
/// @param ty 
static void maybe_emit_bitshift_load(Type *ty) {
    SAVE;
    if (ty->bitsize <= 0)
        return;
    //emit("shr $%d, #rax", ty->bitoff);
    emit_asm(ins_shr, rax, ty->bitoff, rax);
    push(rcx);
    emit_asm(ins_movi, rcx, (1 << (long)ty->bitsize) - 1);
    //emit("mov $0x%lx, #rcx", (1 << (long)ty->bitsize) - 1);
    //emit("and #rcx, #rax");
    emit_ins(ins_and, rcx, rax, rax); //clear rcx
    pop(rcx);
}

// -- 7/22/26 start --

/// @brief potato | emit: merges last bit of rcx with rax, copies addr into free register
/// @param ty 
/// @param addr 
static void maybe_emit_bitshift_save(Type *ty, char *addr) {
    SAVE;
    if (ty->bitsize <= 0)
        return;
    //save rcx and rdi
    push(rcx);
    push(rdi);

    //operation: rax(minus last bit) << bitoff ||  rcx(last bit) << bitoff

    //mask out last bit of rax
    //emit("mov $0x%lx, #rdi", (1 << (long)ty->bitsize) - 1);
    emit_asm(ins_movi, rdi, (1 << (long)ty->bitsize) - 1);
    emit_asm(ins_and, rdi, rax, rax);
    //adjust for offset
    emit_asm(ins_shl, rax, ty->bitoff, rax);
    //copy storage address into free register
    emit_asm(ins_movi, get_int_reg(ty, 'c'), addr, 0);
    //mask out last bit of rcx 
    emit_asm(ins_movi, rdi, ~(((1 << (long)ty->bitsize) - 1) << ty->bitoff), 0);
    emit_asm(ins_and, rdi, rcx, rcx);
    emit_asm(ins_or, rcx, rax, rax);
    pop(rdi);
    pop(rcx);
}

// -- 7/27/26 start --

/// @brief emit: loads global var/array from label into rax
/// @param ty 
/// @param label 
/// @param off 
static void emit_gload(Type *ty, char *label, int off) {
    SAVE;
    if (ty->kind == KIND_ARRAY) {
        if (off)
            //computes gb + label + off and puts into rax
            //emit("lea %s+%d(#rip), #rax", label, off);
            emit_addr(ins_add, gb, label, rax);
            emit_addr(ins_add, rax, off, rax);
        else
            //otherwise, just compute gb + label and put into rax
            //emit("lea %s(#rip), #rax", label);
            emit_addr(ins_add, gb, label, rax);
        return;
    }
    char *inst = get_load_inst(ty);
    //move the data from the label offset into rax
    emit("%s %s+%d(#rip), #rax", inst, label, off);
    maybe_emit_bitshift_load(ty);
}

