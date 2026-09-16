// EX_ISA backend for 8cc compiler
// Target: 4-bit opcodes, 16 registers, 16-bit words, 256-byte address space
// Build compiler: make TARGET=ex-isa
// Compile test: ./8cc -mex-isa -S -o test_ex_isa_codegen.s test_ex_isa_codegen.c
// Width test: ./8cc -mex-isa -S -o test_ex_isa_widths.s test_ex_isa_widths.c
//this project uses the assembler below. all assembly instructions 
//should be converted into this format.

/*
    Simple two-pass assembler for the project's EX_ISA.

    Usage: assembler-EX_ISA <input.asm> <output.txt> [--mif] [--mif-out <output.mif>]
                             [--data-out <data.txt>] [--data-mif-out <data.mif>]

    This machine has split instruction/data memories: <output.txt>/<output.mif> hold the assembled
    instruction stream, while the data section is written to its own plaintext file (default
    <output>.data.txt, override with --data-out) and, with --mif, its own MIF (default
    <output>.data.mif, override with --data-mif-out).

    Assembly syntax (whitespace and commas separate tokens):

    - Registers: R0 .. R15 (case-insensitive) or numeric 0..15. R0 is fixed at zero, R14 is ASM_TMP, and R15 is PC.
      -- Assembly formatting instructions --
      - Use .text for instructions and instruction labels.
      - Use .data for data directives and data labels.
      - Labels end with ':' and may appear on their own line.
      - Tokens are separated by whitespace and/or commas.
      - Comments may start with ';', '//' or '#'.
    - Registers are R0..R15 (case-insensitive).
    - Control-flow labels (JMP/JLT and conditional pseudo-branch forms) must be .text labels.
      - Memory-address labels (STR/LDR label form) must be .data labels.

    .data directives (written to the separate data output/MIF described above):
      .word v1[, v2...]   writes one 16-bit word per value
      .space count        writes count zero words
      .long v1[, v2...]   writes 2 words per 32-bit value, most-significant word first
      .quad v1[, v2...]   writes 4 words per 64-bit value, most-significant word first
      .string "literal"   writes ceil((chars+1)/2) words, packing 2 chars/word (first char in the high
                          byte) plus a null terminator; backslash escapes (\n \t \r \\ \" \0) decode
                          to their real byte value before packing

        Debug metadata directives emitted by the 8cc backend are accepted and do not
        contribute to instruction or data addresses:
            .file number "filename"
            .loc file line [column]

    Instruction formats implemented :

      STR rA, rB/LABEL, soff (store RF[rA] -> D[RF[rB] + soff])
          -> 0001 raaa rbbb soff  offsets are signed 16-bit word displacements; larger values use ASM_TMP
      LDR rA, rB/LABEL, soff (load D[RF[rB] + soff] -> RF[rA])
          -> 0010 raaa rbbb soff  offsets are signed 16-bit word displacements; larger values use ASM_TMP

      COPY rSrc, rDest[, count] (copy count words from RF[rSrc] to RF[rDest])
          -> pseudo-instruction: expands to LDR/STR pairs using ASM_TMP; omitted count means one word
             count is a positive 16-bit value and source/destination registers are restored for long copies

      ADD rA, rB, rC (rA = rB + rC)
          -> 0011 raaa rbbb rccc
      ADDI rA, rB, imm/label (rA = rB + immediate)
          -> pseudo-instruction: MOVI ASM_TMP, immediate; ADD rA, rB, ASM_TMP
      SUB rA, rB, rC (rA = rB - rC)
          -> 0100 raaa rbbb rccc
      SUBI rA, rB, imm/label (rA = rB - immediate)
          -> pseudo-instruction: MOVI ASM_TMP, immediate; SUB rA, rB, ASM_TMP
      HLT                              
          -> 0101 0000 0000 0000

    MOVI rA, imm8_or_label (rA = rA | imm)
          -> 0110 raaa dddddddd     (ORs the immediate value into the selected register, using pseudoins for >8 bits)
             can load a label from either iram or dram.
      OR  rA, rB, rC   (rA = rB | rC)
          -> 0111 raaa rbbb rccc
      ORI rA, rB, imm/label (rA = rB | immediate)
          -> pseudo-instruction: MOVI ASM_TMP, immediate; OR rA, rB, ASM_TMP
      AND rA, rB, rC   (rA = rB & rC)
          -> 1000 raaa rbbb rccc
      ANDI rA, rB, imm/label (rA = rB & immediate)
          -> pseudo-instruction: MOVI ASM_TMP, immediate; AND rA, rB, ASM_TMP

      JMP offset/LABEL (PC = PC + soff12)
          -> 1001 bbbb bbbb bbbb  (signed 12-bit PC-relative offset)
      JMP rA (optional pseudo-form: PC = RF[rA])
          -> copies the register value into the PC register
      JNZ rA, rB, soff4 (PC = RF[rB] + soff4 if RF[rA] != 0)
          -> 1010 raaa rbbb bbbb
      JLT rA, rB, offset (PC = PC + offset if rA < rB)
          -> 1011 raaa rbbb bbbb    (4-bit signed offset relative to next instr)
      JZ rA, label (branch if rA == 0)
      JEQ rA, rB_or_imm, label (branch if rA == rB_or_imm)
      JNE rA, rB_or_imm, label (branch if rA != rB_or_imm)
      JLE rA, rB_or_imm, label (branch if signed rA <= rB_or_imm)
      JGT rA, rB_or_imm, label (branch if signed rA > rB_or_imm)
      JGE rA, rB_or_imm, label (branch if signed rA >= rB_or_imm)
          -> pseudo-instructions: CMP, SETcc, and JNZ via ASM_TMP; immediates are signed 16-bit values

      CMP rA, rB (capture Z, N, and V from signed rA - rB)
          -> 1110 raaa rbbb 0000
      SETLT rD (rD = 1 if the most recent CMP was signed less-than, else 0)
      SETEQ rD (rD = 1 if the most recent CMP was equal, else 0)
      SETNE rD (rD = 1 if the most recent CMP was not equal, else 0)
      SETLE rD (rD = 1 if the most recent CMP was signed less-than or equal, else 0)
      SETGT rD (rD = 1 if the most recent CMP was signed greater-than, else 0)
      SETGE rD (rD = 1 if the most recent CMP was signed greater-than or equal, else 0)
          -> 1110 rddd cccc 1111    (cc: 0=LT, 1=EQ, 2=NE, 3=LE, 4=GT, 5=GE)

      SHL rA, rB, shft (rA = rB << shft)
          -> 1100 raaa shft rccc     (shft is an unsigned 4-bit immediate)
      MULT rA, rB, rC  (rA = rB * rC)
          -> 1101 raaa rbbb rccc    
      MULTI rA, rB, imm/label (rA = rB * immediate)
          -> pseudo-instruction: MOVI ASM_TMP, immediate; MULT rA, rB, ASM_TMP
      SHR rA, rB, shft (rA = rB >> shft)
          -> 0000 raaa shft rccc

      NOP                         
          -> 1000 0000 0000 0000   (AND R0 with R0 into R0, effectively a NOP)
      MOV rA, rB       (rA = rB)
          -> 1000 raaa rbbb rccc    (AND RA with RA into RB, effectively moving) 
      XOR rA, rB, rC   (rA = rB ^ rC)
          -> 1011 raaa rbbb rccc


    The assembler supports labels for PC-relative control flow and computes relative offsets
    as: offset = target_address - (current_address + 1).
    - JMP label uses a signed 12-bit offset (-2048..+2047).
    - JLT label uses a signed 4-bit offset (-8..+7).
*/


//DEFINES: aliases for all instructions in the ISA
#define ins_shr 0x0
#define ins_str 0x1
#define ins_ldr 0x2
#define ins_add 0x3
#define ins_addi 0x11
#define ins_sub 0x4
#define ins_subi 0x12
#define ins_hlt 0x5
#define ins_movi 0x6
#define ins_or 0x7
#define ins_ori 0x13
#define ins_and 0x8
#define ins_andi 0x14
#define ins_jmp 0x9
#define ins_jnz 0xA
#define ins_jlt 0xB
#define ins_shl 0xC
#define ins_mult 0xD
#define ins_multi 0x15
#define ins_nop 0xE
#define ins_mov 0xF
#define ins_xor 0x10

//dedicated registers for stack and frame pointers
#define pc 15       //program counter register
#define asm_tmp 14  //temp register exclusively for assembly to machine code translation. don't use
#define tmp 13      //temp register for extended c translation
#define sp 12       //stack pointer register
#define frame_pointer 11 //frame pointer register
#define gb 10       //global base register
#define zero 0

//x86 registers hardcoded to match EX_ISA registers
#define rax 1   //x86 accumulator register equivalent
#define rbx 2   //x86 base register equivalent
#define rcx 3   //x86 counter register equivalent
#define rdx 4   //x86 data register equivalent
#define eax 5   //x86 accumulator register equivalent (32-bit)
#define rsi 6   //x86 source index register equivalent
#define rdi 7   //x86 destination index register equivalent
#define rbp frame_pointer //x86 base pointer register equivalent
#define rsp sp  //x86 stack pointer register equivalent

#define xmm0 8  //x86 floating point register equivalent (TEMP)
#define xmm1 9  //x86 floating point register equivalent (TEMP)

#define EX_ISA_REG_BYTES 2

// Copyright 2012 Rui Ueyama. Released under the MIT license.

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include <unistd.h>
#include "8cc.h"

bool dumpstack = false;
bool dumpsource = true;

//x86 register names: do we need them?
//REGS[] holds the list of context-dependent registers.
static int REGS[] = {rax, rbx, rcx, rdx, rdi, rsi};
static int FREGS[] = {xmm0, xmm1};
//tab length
static int TAB = 8;
static Vector functions_storage;
//empty vector for compiled functions
static Vector *functions = &functions_storage;
//location of stack
static int stackpos;
//number of global variables
static int numgp;
//number of floating point variables
static int numfp;
//output file path
static FILE *outputfp;
//map of compiled source files and lines
static Map source_files_storage;
static Map source_lines_storage;
static Map *source_files = &source_files_storage;
static Map *source_lines = &source_lines_storage;
//pointer to last line read
static char *last_loc = "";

//forward declarations of expressions and addresses
static void emit_addr(Node *node);
static void emit_expr(Node *node);
static void emit_decl_init(Vector *inits, int off, int totalsize);
static void do_emit_data(Vector *inits, int size, int off, int depth);
static void emit_data(Node *v, int off, int depth);

/// @brief general-purpose and floating-point register lengths
#define GPREG_LENGTH 1 //registers are one word
#define FPREG_LENGTH 1 //fp registers are 2 words ideally, but constrained to one word for now.
//6 GP registers (16 bits : rax, rbx, rcx, rdx, rsi, rdi), with xmm0 and xmm1 (32 bits each) as FP registers
#define REGAREA_SIZE (6 * GPREG_LENGTH + 2 * FPREG_LENGTH)

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
void set_output_file(FILE *outfp) {
    outputfp = outfp;
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


/// @brief emit an EX_ISA instruction with opcode-specific operand count
/// @param op
/// @param a
static void emit_asm(int op, int a, ...) {
    va_list args;
    va_start(args, a);
    int b;
    int c;
    switch (op) {
    case ins_hlt:
        emit("hlt");
        break;
    case ins_nop:
        emit("nop");
        break;
    case ins_jmp:
        emit("jmp %d", a);
        break;
    case ins_movi:
    case ins_mov:
        b = va_arg(args, int);
        if (op == ins_movi)
            emit("movi %d, %d", a, b);
        else
            emit("mov %d, %d", a, b);
        break;
    case ins_shr:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("shr %d, %d, %d", a, b, c);
        break;
    case ins_str:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("str %d, %d, %d", a, b, c);
        break;
    case ins_ldr:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("ldr %d, %d, %d", a, b, c);
        break;
    case ins_add:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("add %d, %d, %d", a, b, c);
        break;
    case ins_addi:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("addi %d, %d, %d", a, b, c);
        break;
    case ins_sub:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("sub %d, %d, %d", a, b, c);
        break;
    case ins_subi:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("subi %d, %d, %d", a, b, c);
        break;
    case ins_or:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("or %d, %d, %d", a, b, c);
        break;
    case ins_ori:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("ori %d, %d, %d", a, b, c);
        break;
    case ins_and:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("and %d, %d, %d", a, b, c);
        break;
    case ins_andi:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("andi %d, %d, %d", a, b, c);
        break;
    case ins_xor:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("xor %d, %d, %d", a, b, c);
        break;
    case ins_jlt:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("jlt %d, %d, %d", a, b, c);
        break;
    case ins_shl:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("shl %d, %d, %d", a, b, c);
        break;
    case ins_mult:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("mult %d, %d, %d", a, b, c);
        break;
    case ins_multi:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("multi %d, %d, %d", a, b, c);
        break;
    case ins_jnz:
        b = va_arg(args, int);
        c = va_arg(args, int);
        emit("jnz %d, %d, %d", a, b, c);
        break;
    default:
        error("Unknown EX_ISA opcode: %d", op);
    }
    va_end(args);
}

/// @brief convert data size to the matching register type
///        a-type or c-type
/// @param ty 
/// @param r 
/// @return 
static int get_int_reg(Type *ty, char r) {
    // The EX_ISA register file has one 16-bit word per integer register.
    assert(r == 'a' || r == 'c');
    if (is_inttype(ty) || ty->kind == KIND_PTR)
        return (r == 'a') ? rax : rcx;
    else
        error("Unknown data size: %s: %d", ty2s(ty), ty->size);
}

/// @brief select the appropriate mov command by bit length (just mov)
/// @param ty 
/// @return 
static int get_load_inst(Type *ty) {
    if (is_inttype(ty) || ty->kind == KIND_PTR)
        return ins_movi;
    else
        error("Unknown data size: %s: %d", ty2s(ty), ty->size);
}

/// @brief normalize an integer value to the EX_ISA register width
/// @param ty source type represented in rax
static void emit_normalize_int(Type *ty) {
    if (!is_inttype(ty))
        return;

    if (ty->size > EX_ISA_REG_BYTES) {
        emit("andi %d, %d, %d", rax, rax, 0xFFFF);
        return;
    }

    if (ty->size < EX_ISA_REG_BYTES) {
        int mask = (ty->kind == KIND_BOOL) ? 1 : 0xFF;
        emit("andi %d, %d, %d", rax, rax, mask);
        if (!ty->usig && ty->kind != KIND_BOOL) {
            char *done = make_label();
            emit("andi %d, %d, %d", tmp, rax, 0x80);
            emit("jz %d, %s", tmp, done);
            emit("subi %d, %d, %d", rax, rax, 0x100);
            emit("%s:", done);
        }
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
    //FPs will use DWORDS, or 2 words
    //subtract 2 from stack pointer (word address in direct memory mapping)
    emit("subi %d, %d, %d", sp, sp, 1); //PROBLEM: sp only contains a register value, isa can only write direct values
                                         //SOLUTION: make str and ldr pull addresses from registers instead of
                                         // direct memory mapping, expanding address space to 16 bit 
                                         //FIXED: this is now fixed in the arch
    //emit("sub $8, #rsp");
    //then store register at stack pointer
    emit_asm(ins_str, FREGS[reg], sp, 0);
    //emit("movsd #xmm%d, (#rsp)", reg);
    stackpos += 2;
}

/// @brief potato | emits: pop a register from the stack, update sp manually
/// @param reg 
static void pop_xmm(int reg) {
    SAVE;
    emit_asm(ins_ldr, FREGS[reg], sp, 0);
    //add 1 to stack pointer
    //emit("movsd (#rsp), #xmm%d", reg);
    emit("addi %d, %d, %d", sp, sp, 1);
    stackpos -= 2;
    assert(stackpos >= 0);
}

// -- 7/17/26 start --

/// @brief potato | emits: push a register to the global stack, update sp
/// @param reg 
static void push(int reg) {
    SAVE;
    emit("subi %d, %d, %d", sp, sp, 1);
    emit("str %d, %d, %d", reg, sp, 0);
    stackpos += 1; //remember, each instruction is 16 bits, so only 1 word
}

/// @brief potato | emits: pop a register from the global stack, update sp
/// @param reg 
static void pop(int reg) {
    SAVE;
    emit("ldr %d, %d, %d", reg, sp, 0);
    emit("addi %d, %d, %d", sp, sp, 1);
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

    emit("subi %d, %d, %d", sp, sp, words + 1);
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
    emit_asm(ins_shr, rax, rax, ty->bitoff);
    //emit("mov $0x%lx, #rcx", (1 << (long)ty->bitsize) - 1);
    //emit("and #rcx, #rax");
    emit("andi %d, %d, %d", rax, rax, (1 << (long)ty->bitsize) - 1);
}

// -- 7/22/26 start --

/// @brief potato | emit: merges last bit of rcx with rax, copies addr into free register
/// @param ty 
static void maybe_emit_bitshift_save(Type *ty) {
    SAVE;
    if (ty->bitsize <= 0)
        return;
    //save rcx and rdi
    push(rcx);

    //operation: rax(minus last bit) << bitoff ||  rcx(last bit) << bitoff

    //mask out last bit of rax
    //emit("mov $0x%lx, #rdi", (1 << (long)ty->bitsize) - 1);
    emit("andi %d, %d, %d", rax, rax, ((1 << (long)ty->bitsize) - 1));
    //adjust for offset
    emit_asm(ins_shl, rax, rax, ty->bitoff);
    //mask out last bit of rcx 
    emit("andi %d, %d, %d", rcx, rcx, ~(((1 << (long)ty->bitsize) - 1) << ty->bitoff) & 0xFFFF);

    emit_asm(ins_or, rcx, rax, rax);
    pop(rcx);
}

// -- 7/27/26 start --
// -- 7/28/26 start --

/*
TODO: global variables require Position Independent Code (PIC)
to function. the current ISA has JMP and JNZ requiring absolute
addresses, not relative addresses, to function. gload() will require:
a) labels to be resolved into an absolute offset plus a PC-relative value.
(ie. how far from the current instruction does the PC need to move?)
then, jumps can be made by adding or subtracting the value from the PC.
this causes all programs to be dependent ONLY on the PC, and not on jumps to
labels marked at absolute locations in the code.
*/

/*
FIX: JMP and JNZ now use relative offsets, labels, or register data
to determine jumps. with this, the entire ISA now solely supports relative
addessing and is prepared for PIC conversion.
*/

/*
loads a global variable from a global buffer address + label offset into rax.
if the variable is an array, it will load the address of the array into rax.
*/

/// @brief potato | emit: loads global var/array from label into rax
/// @param ty 
/// @param label 
/// @param off 
static void emit_gload(Type *ty, char *label, int off) {
    SAVE;
    if (ty->kind == KIND_ARRAY) {
        // MOVI ORs its immediate into the destination, so clear rax before
        // materializing the array's data-label address.
        emit_asm(ins_mov, rax, zero);
        emit("movi %d, %s", rax, label);
        if (off) {
            // Form label + off so rax holds the address of the array element.
            emit("addi %d, %d, %d", rax, rax, off);
        }
        return;
    }

    // Load the scalar value at data label + off directly into rax. The
    // assembler resolves the data label and expands the address load as needed.
    emit("ldr %d, %s, %d", rax, label, off);
    // Extract the requested bit-field after its containing word is loaded.
    maybe_emit_bitshift_load(ty);
    emit_normalize_int(ty);

}

// -- 8/18/26 start --

/*
ISA note: our isa only works with 16-bit registers,
so there's no need to move char, short, or int data from different-length registers based on type.
the function determines moves based on the sign as well, but since the register lengths aren't being
extended in any move, we can ignore this as both input and output will be 16 bits.
in this stage, this function isn't needed, but the structure will be kept so that the original
handling can be observed if we want to increase/variate register sizes.
*/

/// @brief potato | emit: converts data in e/rax to int based on type
/// @param ty 
static void emit_intcast(Type *ty) {
    emit_normalize_int(ty);
}

/*
once again, there is no support for floating point math, so this function
only exists to demonstrate the code's previous handling strategy.
*/

/// @brief potato | emit: forcibly trims float and double in eax to int
/// @param ty 
static void emit_toint(Type *ty) {
    SAVE;
    if (ty->kind == KIND_FLOAT)
        emit_asm(ins_mov, rax, rax);
    else if (ty->kind == KIND_DOUBLE)
        emit_asm(ins_mov, rax, rax);
}

/// @brief potato | emit: loads local variable (array, int, float, double) from base+offset into rax
/// @param ty 
/// @param base 
/// @param off 
static void emit_lload(Type *ty, int base, int off) {
    SAVE;
    if (ty->kind == KIND_ARRAY) {
        // Copy the local frame or pointer base into rax; arrays evaluate to
        // their address rather than loading a value from that address.
        emit_asm(ins_mov, rax, base);
        if (!off)
            return;

        // MOVI ORs the immediate into TMP, so we clear TMP before loading the
        // magnitude of the array-element displacement.
        emit_asm(ins_mov, tmp, zero);

        if (off < 0) {
            // Subtract the negative displacement to form base + off in rax.
            emit_asm(ins_subi, rax, rax, off < 0 ? -off : off);
        } else {
            // Add the positive displacement to form base + off in rax.
            emit_asm(ins_addi, rax, rax, off < 0 ? -off : off);
        }
        return;
    }

    // LDR's assembler pseudo-instruction accepts the full signed 16-bit
    // displacement and lowers base + off to the necessary instruction sequence.
    emit_asm(ins_ldr, rax, base, off);

    // Extract the requested bit-field after loading its containing word.
    maybe_emit_bitshift_load(ty);
    emit_normalize_int(ty);

}

/*
ISA note: once again, we're using the exact same register types, so converting a 
type that contains '1' to a boolean requires alomst no effort.
*/

/// @brief potato | emit: converts rax into boolean
/// @param ty 
static void maybe_convert_bool(Type *ty) {
    if (ty->kind == KIND_BOOL) {
        //sets the test bits for RAX.
        //if they're not equal, 
        //emit("test #rax, #rax");
        //emit("setne #al");

        //if the value in rax is not zero, ser rax to 1. otherwise, set to 0.
        emit_asm(ins_mov, tmp, zero); //set tmp to 0
        emit_asm(ins_jnz, rax, tmp, 2); //jump to set rax to 1
        emit_asm(ins_mov, rax, zero); //set rax to 0
        emit_asm(ins_jmp, 3); //jump past end to next instruction
        emit_asm(ins_movi, tmp, 1); //set tmp to 1
        emit_asm(ins_mov, rax, tmp); //set rax to 1
    }
}

// -- 8/21/26 start --

/// @brief potato | emit: saves global variable back to memory as variable
/// @param varname 
/// @param ty 
/// @param off 
static void emit_gsave(char *varname, Type *ty, int off) {
    SAVE;
    //assert that the type isn't an array, since arrays are pointers and can't be saved
    assert(ty->kind != KIND_ARRAY);
    //convert any booleans into usable variables
    maybe_convert_bool(ty);
    emit_normalize_int(ty);
    //get the appropriate register for the type
    int reg = get_int_reg(ty, 'a');
    if (ty->bitsize > 0) {
        // Bit-field stores must preserve neighboring bits in the same word.
        emit("ldr %d, %s, %d", rcx, varname, off);
        maybe_emit_bitshift_save(ty);
    }
    // Store to data label + offset using EX_ISA assembler label addressing.
    emit("str %d, %s, %d", reg, varname, off);
}

/// @brief potato | emit: saves local variable in xmm0 to memory
/// @param ty the type of the local variable
/// @param off the offset from the base pointer where the local variable is stored
static void emit_lsave(Type *ty, int off) {
    SAVE;
    if (ty->kind == KIND_FLOAT) {
        // if the type is a float,
        //emit("movss #xmm0, %d(#rbp)", off);
        emit_asm(ins_str, rax, rbp, off);
    } else if (ty->kind == KIND_DOUBLE) {
        // if the type is a double,
        //emit("movsd #xmm0, %d(#rbp)", off);
        emit_asm(ins_str, rax, rbp, off);
    } else {
        //otherwise, convert booleans to usable state
        maybe_convert_bool(ty);
        emit_normalize_int(ty);
        //get appropriate register type,
        int reg = get_int_reg(ty, 'a');
        if (ty->bitsize > 0) {
            // Read-modify-write for local bit-field updates.
            emit_asm(ins_ldr, rcx, rbp, off);
            maybe_emit_bitshift_save(ty);
        }
        // Store using base-register + displacement (EX_ISA-native form).
        emit_asm(ins_str, reg, rbp, off);
    }
}

/// @brief potato | emit: dereference value from utility registers to rax addr
/// @param ty 
/// @param off 
static void do_emit_assign_deref(Type *ty, int off) {
    SAVE;
    /*
    (we push rcx to the stack, get a free register,
    then store the value to the address in rax + offset,
    then pop rcx back to the stack)
    */
    // The address was pushed before the value expression was evaluated.
    emit_asm(ins_ldr, rcx, sp, 0);

    emit_normalize_int(ty);
    int reg = get_int_reg(ty, 'a');
    if (off)
        emit_asm(ins_str, reg, rcx, off);
    else
        emit_asm(ins_str, reg, rcx, 0);
    pop(rax);
}

/// @brief potato | emit: helper: push rax, deref pointer with offset 0
/// @param var 
static void emit_assign_deref(Node *var) {
    SAVE;
    push(rax);
    emit_expr(var->operand);
    do_emit_assign_deref(var->operand->ty->ptr, 0);
}

/// @brief potato | emit: do arithmetic (add, sub) on two pointers
/// @param kind 
/// @param left 
/// @param right 
static void emit_pointer_arith(char kind, Node *left, Node *right) {
    SAVE;
    emit_expr(left);
    push(rcx);
    push(rax);
    emit_expr(right);
    int size = left->ty->ptr->size;
    if (size > 1) {
        emit("multi %d, %d, %d", rax, rax, size);
    }
    emit_asm(ins_mov, rcx, rax);
    pop(rax);
    switch (kind) {
    case '+': emit("add %d, %d, %d", rcx, rax, rax); break;
    case '-': emit("sub %d, %d, %d", rcx, rax, rax); break;
    default: error("invalid operator '%d'", kind);
    }
    pop(rcx);
}

// -- 8/22/26 start --

/// @brief potato | emit: blank out portions of memory
/// @param start 
/// @param end 
static void emit_zero_filler(int start, int end) {
    SAVE;
    for (; start < end; start++)
        emit_asm(ins_str, zero, rbp, start);
    /*
    we don't have a memory unit apart from 16 bit, so all fills
    and variables will operate in 16 bit units regardless.
    */

    //for (; start < end; start++)
    //    emit("movb $0, %d(#rbp)", start);
}

/// @brief potato | check the localvar initialized
/// @param node 
static void ensure_lvar_init(Node *node) {
    SAVE;
    assert(node->kind == AST_LVAR);
    if (node->lvarinit)
        emit_decl_init(node->lvarinit, node->loff, node->ty->size);
    node->lvarinit = NULL;
}

/// @brief potato | create a reference to a struct based on its type, or deref
/// @param struc 
/// @param field 
/// @param off 
static void emit_assign_struct_ref(Node *struc, Type *field, int off) {
    SAVE;
    switch (struc->kind) {
    case AST_LVAR:
        ensure_lvar_init(struc);
        emit_lsave(field, struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gsave(struc->glabel, field, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_assign_struct_ref(struc->struc, field, off + struc->ty->offset);
        break;
    case AST_DEREF:
        push(rax);
        emit_expr(struc->operand);
        do_emit_assign_deref(field, field->offset + off);
        break;
    default:
        error("internal error: %s", node2s(struc));
    }
}

/// @brief potato | loads a struct reference from memory based on type
/// @param struc 
/// @param field 
/// @param off 
static void emit_load_struct_ref(Node *struc, Type *field, int off) {
    SAVE;
    switch (struc->kind) {
    case AST_LVAR:
        ensure_lvar_init(struc);
        emit_lload(field, rbp, struc->loff + field->offset + off);
        break;
    case AST_GVAR:
        emit_gload(field, struc->glabel, field->offset + off);
        break;
    case AST_STRUCT_REF:
        emit_load_struct_ref(struc->struc, field, struc->ty->offset + off);
        break;
    case AST_DEREF:
        emit_expr(struc->operand);
        emit_lload(field, rax, field->offset + off);
        break;
    default:
        error("internal error: %s", node2s(struc));
    }
}

/// @brief potato | stores a struct, lvar, gvar or deref based on type
/// @param var 
static void emit_store(Node *var) {
    SAVE;
    switch (var->kind) {
    case AST_DEREF: emit_assign_deref(var); break;
    case AST_STRUCT_REF: emit_assign_struct_ref(var->struc, var->ty, 0); break;
    case AST_LVAR:
        ensure_lvar_init(var);
        emit_lsave(var->ty, var->loff);
        break;
    case AST_GVAR: emit_gsave(var->glabel, var->ty, 0); break;
    default: error("internal error");
    }
}

/*
this leads to a problem with the ALU. we're ignoring FP support as of now,
but the set and compare instructions aren't supported. The alu doesn't latch 
the status bits for multistep comparison, so it has to
be changed to support that without conflicting with preexisting instructions.

the problem is fixed! we now support set and cmp instructions.
*/

/// @brief potato | emit: convert a local variable to boolean (check 1/0)
/// @param ty 
static void emit_to_bool(Type *ty) {
    SAVE;
    if (is_flotype(ty)) {
        //push the variable to the stack
        push_xmm(xmm1);
        //xor a copy with itself (blank it out)
        emit("xor %d, %d, %d", xmm1, xmm1, xmm1);
        //compare it with xmm0 (what is it?) according to its type
        //emit("%s #xmm1, #xmm0", (ty->kind == KIND_FLOAT) ? "ucomiss" : "ucomisd");
        emit("cmp %d, %d", xmm1, xmm0);
        //set the alu status register if not equal.

        emit("setne %d", rax);
        pop_xmm(xmm1);
    } else {
        emit("cmp %d, %d", rax, zero);
        emit("setne %d", rax);
    }
    emit_asm(ins_mov, rax, rax);
}

/// @brief potato | emit: compare local variable to instance (float specific)
/// @param inst 
/// @param usiginst 
/// @param node 
static void emit_comp(char *inst, char *usiginst, Node *node) {
    SAVE;
    if (is_flotype(node->left->ty)) {
        emit_expr(node->left);
        push_xmm(0);
        emit_expr(node->right);
        pop_xmm(1);
        if (node->left->ty->kind == KIND_FLOAT)
            emit("cmp %d, %d", xmm0, xmm1);
        else
            emit("cmp %d, %d", xmm0, xmm1);
    } else {
        emit_expr(node->left);
        push(rax);
        emit_expr(node->right);
        pop(rcx);
                emit("cmp %d, %d", rax, rcx);
    }
    if (is_flotype(node->left->ty) || node->left->ty->usig)
    //THIS ISNT SUPPORTED INSTRUCTION
        emit("%s %d", usiginst, rax);
    else
        emit("%s %d", inst, rax);
    emit_asm(ins_mov, rax, rax);
}

/*
note: the ISA doesn't support division or modulus,
so these operations are not implemented. future support may be added.
SAL and SAR map to the ISA's SHL and SHR.
*/

/// @brief potato | emit: integer arithmetic!!
/// @param node 
static void emit_binop_int_arith(Node *node) {
    SAVE;
    char *op = NULL;
    switch (node->kind) {
    case '+': op = "add"; break;
    case '-': op = "sub"; break;
    case '*': op = "mult"; break;
    case '^': op = "xor"; break;
    case OP_SAL: op = "shl"; break;
    case OP_SAR: op = "shr"; break;
    case OP_SHR: op = "shr"; break;
    case '/': case '%': break;
    default: error("invalid operator '%d'", node->kind);
    }
    emit_expr(node->left);
    push(rax);
    emit_expr(node->right);
    emit_asm(ins_mov, rcx, rax);
    pop(rax);
    if (node->kind == '/' || node->kind == '%') {
        if (node->ty->usig) {
          //emit("xor #edx, #edx");
          //emit("div #rcx");
          error("unsupported operation '%c'", node->kind);
        } else {
          //emit("cqto");
          //emit("idiv #rcx");
          error("unsupported operation '%c'", node->kind);
        }
        if (node->kind == '%')
            error("unsupported operation '%c'", node->kind);
            //emit("mov #edx, #eax");
    } else if (node->kind == OP_SAL || node->kind == OP_SAR || node->kind == OP_SHR) {
        //emit("%s #cl, #%s", op, get_int_reg(node->left->ty, 'a'));
        emit("%s %d, %d, %d", op, rax, rax, rcx);
    } else {
        emit("%s %d, %d, %d", op, rax, rax, rcx);
    }
}

/*
we don't have any float support, so none of this gets used.
it'll be kept for reference when float support is added.
*/

/// @brief potato | emit: floating-point arithmetic
/// @param node 
static void emit_binop_float_arith(Node *node) {
    SAVE;
    char *op;
    bool isdouble = (node->ty->kind == KIND_DOUBLE);
    fprintf(stderr, "floating point arithmetic is not supported in this ISA\n");
    /*
    switch (node->kind) {
    case '+': op = (isdouble ? "addsd" : "addss"); break;
    case '-': op = (isdouble ? "subsd" : "subss"); break;
    case '*': op = (isdouble ? "mulsd" : "mulss"); break;
    case '/': op = (isdouble ? "divsd" : "divss"); break;
    default: error("invalid operator '%d'", node->kind);
    }
    
    emit_expr(node->left);
    push_xmm(0);
    emit_expr(node->right);
    emit("%s #xmm0, #xmm1", (isdouble ? "movsd" : "movss"));
    pop_xmm(0);
    emit("%s #xmm1, #xmm0", op);
    */

    //stand-in operation redirect for FP arithmetic
    //
    switch (node->kind) {
    case '+': op = "add"; 
        break;
    case '-': op = "sub";
        break;
    case '*': op = "mult";
        break;
    case '/': op = "div";
        break;
    default: 
        error("invalid operator '%d'", node->kind);
    }

    emit_expr(node->left);
    push_xmm(0);
    emit_expr(node->right);
    emit("mov %d, %d", xmm1, xmm0);
    pop_xmm(0);
    emit("%s %d, %d", op, xmm0, xmm1);
}

/// @brief potato | emit: load and convert data between types
/// @param to 
/// @param from 
static void emit_load_convert(Type *to, Type *from) {
    SAVE;
    if (is_inttype(from) && to->kind == KIND_FLOAT)
        //we convert and int to a float by moving it from a std reg to a float reg
        //emit("cvtsi2ss #eax, #xmm0");
        emit("mov eax, xmm0");
    else if (is_inttype(from) && to->kind == KIND_DOUBLE)
        //same story for a double
        //emit("cvtsi2sd #eax, #xmm0");
        emit("mov eax, xmm0");
    else if (from->kind == KIND_FLOAT && to->kind == KIND_DOUBLE)
        //really nothing happens in regards to the registers.
        //there's no double or float distinction in the ISA.
        //emit("cvtps2pd #xmm0, #xmm0");
        return;
    else if ((from->kind == KIND_DOUBLE || from->kind == KIND_LDOUBLE) && to->kind == KIND_FLOAT)
        //once again, there's no distinction to us.
        //emit("cvtpd2ps #xmm0, #xmm0");
        return;
    else if (to->kind == KIND_BOOL)
        //now, booleans we have handled already.
        emit_to_bool(from);
    else if (is_inttype(from) && is_inttype(to))
        emit_intcast(to);
    else if (is_inttype(to))
        emit_toint(from);
}

/*
in the ISA, there's no assembly leave or return instruction.
we just have to manually pop the stack and return.
*/

/// @brief  emit: return call
static void emit_ret() {
    SAVE;
    // The existing frame convention stores caller FP at FP[0] and the
    // return PC at FP[1]. Restore the frame and jump through that PC.
    emit_asm(ins_mov, sp, frame_pointer);
    emit_asm(ins_ldr, frame_pointer, sp, 0);
    emit_asm(ins_ldr, tmp, sp, 1);
    emit_asm(ins_addi, sp, sp, 2);
    emit_asm(ins_jmp, tmp);
}

/// @brief potato | emit: binop comparison (LT LE EQ NE)
/// @param node 
static void emit_binop(Node *node) {
    SAVE;
    if (node->ty->kind == KIND_PTR) {
        emit_pointer_arith(node->kind, node->left, node->right);
        return;
    }
    switch (node->kind) {
    case '<': emit_comp("setlt", "setb", node); return;
    case OP_EQ: emit_comp("seteq", "sete", node); return;
    case OP_LE: emit_comp("setle", "setna", node); return;
    case OP_NE: emit_comp("setne", "setne", node); return;
    }
    if (is_inttype(node->ty))
        emit_binop_int_arith(node);
    else if (is_flotype(node->ty))
        emit_binop_float_arith(node);
    else
        error("internal error: %s", node2s(node));
}

// -- 9/3/26 start --

/// @brief potato | emit: save a literal primitive to memory
/// @param node 
/// @param totype 
/// @param off 
static void emit_save_literal(Node *node, Type *totype, int off) {
    switch (totype->kind) {
    //switch based on kind. we store the values to BP with an offset passed in.

    //booleans are stored the same as char/int/long, and the compiler moves a byte.
    // the '!!' operator acts as an existence operator, returning 1 for x != 0.
    // this allows us to do conversion and storage in one line.
    case KIND_BOOL:{
        //emit("movb $%d, %d(#rbp)", !!node->ival, off);
        emit_asm(ins_movi, rax, !!node->ival);
        emit_normalize_int(totype);
        emit_asm(ins_str, rax, rbp, off);
        break;
    }  
    //chars are also stored as a byte
    case KIND_CHAR:{
        //emit("movb $%d, %d(#rbp)", node->ival, off);
        emit_asm(ins_movi, rax, node->ival);
        emit_normalize_int(totype);
        emit_asm(ins_str, rax, rbp, off);
        
        break;
    }
    //shorts are stored as a half-word. (technically that's all we have)
    case KIND_SHORT: {
        //emit("movw $%d, %d(#rbp)", node->ival, off);
        emit_asm(ins_movi, rax, node->ival);
        emit_normalize_int(totype);
        emit_asm(ins_str, rax, rbp, off);
        break;
    }
    //ints are stored as a lower word (32 bits). we still store the whole 16-bit register.
    case KIND_INT: {
        //emit("movl $%d, %d(#rbp)", node->ival, off);
        emit_asm(ins_movi, rax, node->ival);
        emit_normalize_int(totype);
        emit_asm(ins_str, rax, rbp, off);
        break;
    }
    //longs, long longs and pointers are stored as two words, ie 64 bits split into two 32-bit words.
    //our memory is word-addressed, so we offset by one address instead of 4 bytes.
    case KIND_LONG:
    case KIND_LLONG:
    case KIND_PTR: {
        //emit("movl $%lu, %d(#rbp)", ((uint64_t)node->ival) & ((1L << 32) - 1), off);
        //emit("movl $%lu, %d(#rbp)", ((uint64_t)node->ival) >> 32, off + 4);
        emit_asm(ins_movi, rax, (int)((uint64_t)node->ival & 0xFFFF));
        emit_asm(ins_str, rax, rbp, off);
        emit_asm(ins_movi, rax, (int)(((uint64_t)node->ival >> 16) & 0xFFFF));
        emit_asm(ins_str, rax, rbp, off + 1);
        break;
    }
    //floats are stored as one word.
    //they're first cast to a uint32, then stored.
    //yes, this doesn't fit into our memory cleanly, but this will work for the moment
    //and will be addressed when we implement floating point support in the ISA.
    case KIND_FLOAT: {
        float fval = node->fval;
        //emit("movl $%u, %d(#rbp)", *(uint32_t *)&fval, off);
        emit_asm(ins_movi, rax, (int)(*(uint32_t *)&fval & 0xFFFF));
        emit_asm(ins_str, rax, rbp, off);
        break;
    }
    //doubles are stored similarly, but as two words through a uint64.
    //our memory is word-addressed, so we offset by one address instead of 4 bytes.
    case KIND_DOUBLE:
    case KIND_LDOUBLE: {
        //emit("movl $%lu, %d(#rbp)", *(uint64_t *)&node->fval & ((1L << 32) - 1), off);
        //emit("movl $%lu, %d(#rbp)", *(uint64_t *)&node->fval >> 32, off + 4);
        uint64_t bits = *(uint64_t *)&node->fval;
        emit_asm(ins_movi, rax, (int)(bits & 0xFFFF));
        emit_asm(ins_str, rax, rbp, off);
        emit_asm(ins_movi, rax, (int)((bits >> 16) & 0xFFFF));
        emit_asm(ins_str, rax, rbp, off + 1);
        break;
    }
    //default case.
    default:
        error("internal error: <%s> <%s> <%d>", node2s(node), ty2s(totype), off);
    }
}


/// @brief potato | emit: emit address for a variable (local global struct func)
/// @param node 
static void emit_addr(Node *node) {
    switch (node->kind) {
    //we emit the address of a variable based on type.
    case AST_LVAR:
        //local variables are stored on the stack,
        //so we find the address based on its offset from the base
        //pointer.
        ensure_lvar_init(node);
        //emit("lea %d(#rbp), #rax", node->loff);
        //add the absolute address to the base pointer to
        //get the absolute address of the variable.

        //add the offset to the base
        emit("addi %d, %d, %d", rax, rbp, node->loff);
        break;
    case AST_GVAR:
        //global variables are stored elsewhere, so we
        //use the global label and the instruction pointer to
        //find the absolute address.
        //emit("lea %s(#rip), #rax", node->glabel);
        // Data labels are in the separate data address space.
        emit_asm(ins_mov, rax, zero);
        emit("movi %d, %s", rax, node->glabel);
        break;
    case AST_DEREF:
        //if we want to dereference a pointer,
        //we emit the related expression to get the address
        emit_expr(node->operand);
        break;
    case AST_STRUCT_REF:
        emit_addr(node->struc);
        //emit("add $%d, #rax", node->ty->offset);
        emit("addi %d, %d, %d", rax, rax, node->ty->offset);
        break;
    case AST_FUNCDESG:
        //emit("lea %s(#rip), #rax", node->fname);

        emit_asm(ins_mov, rax, zero);
        emit("movi %d, %s", rax, node->fname);
        break;
    default:
        error("internal error: %s", node2s(node));
    }
}

/*
use a rootfinding-style method to procedurally copy

*/

// -- 9/4/26 start --

/// @brief potato | emit: duplicate a struct in memory
/// @param left 
/// @param right 
static void emit_copy_struct(Node *left, Node *right) {
    push(rcx);
    emit_addr(right);
    emit("mov %d, %d", rcx, rax);
    emit_addr(left);
    emit("copy %d, %d, %d", rcx, rax, align(left->ty->size, 2) / 2);
    pop(rcx);
}

/// @brief potato | compare initialization offsets
/// @param x 
/// @param y 
/// @return 
static int cmpinit(const void *x, const void *y) {
    Node *a = *(Node **)x;
    Node *b = *(Node **)y;
    return a->initoff - b->initoff;
}

/// @brief potato | blank unfilled fields of variable with at least one field init and fill passed fields
/// @param inits 
/// @param off 
/// @param totalsize 
static void emit_fill_holes(Vector *inits, int off, int totalsize) {
    // If at least one of the fields in a variable are initialized,
    // unspecified fields has to be initialized with 0.
    int len = vec_len(inits);
    Node **buf = malloc(len * sizeof(Node *));
    for (int i = 0; i < len; i++)
        buf[i] = vec_get(inits, i);
    qsort(buf, len, sizeof(Node *), cmpinit);

    int lastend = 0;
    for (int i = 0; i < len; i++) {
        Node *node = buf[i];
        if (lastend < node->initoff)
            emit_zero_filler(lastend + off, node->initoff + off);
        lastend = node->initoff + node->totype->size;
    }
    emit_zero_filler(lastend + off, totalsize + off);
}

/// @brief potato | emit: variable declaration initialization. creates and saves a variable
/// @param inits 
/// @param off 
/// @param totalsize 
static void emit_decl_init(Vector *inits, int off, int totalsize) {
    emit_fill_holes(inits, off, totalsize);
    for (int i = 0; i < vec_len(inits); i++) {
        Node *node = vec_get(inits, i);
        assert(node->kind == AST_INIT);
        bool isbitfield = (node->totype->bitsize > 0);
        if (node->initval->kind == AST_LITERAL && !isbitfield) {
            emit_save_literal(node->initval, node->totype, node->initoff + off);
        } else {
            emit_expr(node->initval);
            emit_lsave(node->totype, node->initoff + off);
        }
    }
}

/// @brief potato | emit: variable instruction for ++x --x
/// @param node 
/// @param op 
static void emit_pre_inc_dec(Node *node, char *op) {
    emit_expr(node->operand);
    //emit("%s %d, %d", op, node->ty->ptr ? node->ty->ptr->size : 1, rax);
    emit("%s %d, %d, %d", op, rax, node->ty->ptr ? node->ty->ptr->size : 1, rax);
    emit_store(node->operand);
}

//note: the increment and decrement instruction relies on 'op' strings being valid in the ISA.
//there's no guarantee that they'll be valid. looking at their uses,
//they only apply add or sub

/// @brief potato | emit: variable instruction for x++ x--
/// @param node 
/// @param op 
static void emit_post_inc_dec(Node *node, char *op) {
    SAVE;
    emit_expr(node->operand);
    push(rax);
    //emit("%s %d, %d", op, node->ty->ptr ? node->ty->ptr->size : 1, rax);
    emit("%s %d, %d, %d", op, rax, node->ty->ptr ? node->ty->ptr->size : 1, rax);
    emit_store(node->operand);
    pop(rax);
}

/// @brief potato | sort amount of floating point versus int in a vector
/// @param args 
static void set_reg_nums(Vector *args) {
    numgp = numfp = 0;
    for (int i = 0; i < vec_len(args); i++) {
        Node *arg = vec_get(args, i);
        if (is_flotype(arg->ty))
            numfp++;
        else
            numgp++;
    }
}

/// @brief potato | emit: test rax then jump to label on equal
/// @param label 
static void emit_je(char *label) {
    emit("jz %d, %s", rax, label);
}

/// @brief potato | emit: ASM label
/// @param label 
static void emit_label(char *label) {
    emit("%s:", label);
}

/// @brief potato | emit: jump to label
/// @param label 
static void emit_jmp(char *label) {
    emit("jmp %s", label);
}

/// @brief potato | emit: copy literal into registers according to type
/// @param node 
static void emit_literal(Node *node) {
    SAVE;
    switch (node->ty->kind) {
    case KIND_BOOL:
    case KIND_CHAR:
    case KIND_SHORT:
        //int and short are treated the same in this isa due to the word length.
        //moves
        //emit("mov $%u, #rax", node->ival);
        emit("movi %d, %u", rax, node->ival);

        break;
    case KIND_INT:
        //copy an int into a register.
        //emit("mov $%u, #rax", node->ival);
        emit("movi %d, %u", rax, node->ival);
        break;
    case KIND_LONG:
    case KIND_LLONG: {
        //move a long long into a register.
        //it must be trimmed to 16 bits to fit, though.
        emit("movi %d, %d", rax, (node->ival & 0xFFFF));
        break;
    }
    case KIND_FLOAT: {
        //this uses a hack to store the float in memory, then copy it into
        //a floating-point register. since there's no FP support,
        //we just trim it to 16 bits and store in a dummy fp reg.
        /*
        if (!node->flabel) {
            node->flabel = make_label();
            float fval = node->fval;
            //we switch to data section,
            emit_noindent(".data");
            emit_label(node->flabel);
            //store float as a long,
            emit(".long %d", *(uint32_t *)&fval);
            //then return to text section.
            emit_noindent(".text");
        }
        emit("movss %s(#rip), #xmm0", node->flabel);
        */
        emit("movi %d, %d", xmm0, (int)(node->fval) & 0xFFFF); // store float as int for now
        break;
    }
    case KIND_DOUBLE:
    case KIND_LDOUBLE: {
        //the same trick is used here. we'll cap it 
        //the same way floats are.
        /*
        if (!node->flabel) {
            node->flabel = make_label();
            emit_noindent(".data");
            emit_label(node->flabel);
            emit(".quad %lu", *(uint64_t *)&node->fval);
            emit_noindent(".text");
        }
        emit("movsd %s(#rip), #xmm0", node->flabel);
        */
        emit("movi %d, %d", xmm0, (int)(node->fval) & 0xFFFF); // store double as int for now
        break;
    }
    case KIND_ARRAY: {
        //arrays are weird. they must inherently be stored in memory,
        //so we use the hack out of necessity this time.
        if (!node->slabel) {
            //make a label for the string
            node->slabel = make_label();
            //open data section
            emit_noindent(".data");
            //emit the label
            emit_label(node->slabel);
            //emit the string
            emit(".string \"%s\"", quote_cstring_len(node->sval, node->ty->size - 1));
            //return to text section
            emit_noindent(".text");
        }
        //load the address of the string to rax
        //emit("lea %s(#rip), #rax", node->slabel);
        emit_asm(ins_mov, rax, zero);
        emit("movi %d, %s", rax, node->slabel);
        break;
    }
    default:
        error("internal error");
    }
    emit_normalize_int(node->ty);
}

/// @brief potato | count the number of lines in a buffer, then split the buffer into a set of null-terminated lines
/// @param buf 
/// @return 
static char **split(char *buf) {
    char *p = buf;
    int len = 1;
    while (*p) {
        if (p[0] == '\r' && p[1] == '\n') {
            len++;
            p += 2;
            continue;
        }
        if (p[0] == '\r' || p[0] == '\n')
            len++;
        p++;
    }
    p = buf;
    char **r = malloc(sizeof(char *) * len + 1);
    int i = 0;
    while (*p) {
        if (p[0] == '\r' && p[1] == '\n') {
            p[0] = '\0';
            p += 2;
            r[i++] = p;
            continue;
        }
        if (p[0] == '\r' || p[0] == '\n') {
            p[0] = '\0';
            r[i++] = p + 1;
        }
        p++;
    }
    r[i] = NULL;
    return r;
}

/// @brief potato | read a source file
/// @param file 
/// @return 
static char **read_source_file(char *file) {
    FILE *fpointer = fopen(file, "r");
    if (!fpointer)
        return NULL;
    struct stat st;
    fstat(fileno(fpointer), &st);
    char *buf = malloc(st.st_size + 1);
    if (fread(buf, 1, st.st_size, fpointer) != st.st_size) {
        fclose(fpointer);
        return NULL;
    }
    fclose(fpointer);
    buf[st.st_size] = '\0';
    return split(buf);
}

/// @brief potato | print a specific line in a source file
/// @param file 
/// @param line 
static void maybe_print_source_line(char *file, int line) {
    if (!dumpsource)
        return;
    char **lines = map_get(source_lines, file);
    if (!lines) {
        lines = read_source_file(file);
        if (!lines)
            return;
        map_put(source_lines, file, lines);
    }
    int len = 0;
    for (char **p = lines; *p; p++)
        len++;
    emit_nostack("# %s", lines[line - 1]);
}

/// @brief potato | print a specific line in a source file
/// @param node 
static void maybe_print_source_loc(Node *node) {
    if (!node->sourceLoc)
        return;
    char *file = node->sourceLoc->file;
    long fileno = (long)map_get(source_files, file);
    if (!fileno) {
        fileno = map_len(source_files) + 1;
        map_put(source_files, file, (void *)fileno);
        //.file directive?
        emit(".file %ld \"%s\"", fileno, quote_cstring(file));
    }
    char *loc = format(".loc %ld %d 0", fileno, node->sourceLoc->line);
    if (strcmp(loc, last_loc)) {
        emit("%s", loc);
        maybe_print_source_line(file, node->sourceLoc->line);
    }
    last_loc = loc;
}

/// @brief potato | emit: load and check local variable initialization
/// @param node 
static void emit_lvar(Node *node) {
    SAVE;
    ensure_lvar_init(node);
    emit_lload(node->ty, rbp, node->loff);
}

/// @brief potato | emit: load global variable
/// @param node 
static void emit_gvar(Node *node) {
    SAVE;
    emit_gload(node->ty, node->glabel, 0);
}

/// @brief potato | emit: load the return address of a function call
/// @param node 
static void emit_builtin_return_address(Node *node) {
    push(tmp);
    assert(vec_len(node->args) == 1);
    emit_expr(vec_head(node->args));
    char *loop = make_label();
    char *end = make_label();
    //backup base pointer to tmp
    emit("mov %d, %d", rbp, tmp);
    //make a looping label
    emit_label(loop);
    //If the requested frame is reached, return its saved address.
    emit_je(end);
    //Walk to the caller's frame and decrement the requested depth.
    emit("ldr %d, %d, %d", tmp, tmp, 0);
    emit("subi %d, %d, %d", rax, rax, 1);
    emit_jmp(loop);
    emit_label(end);
    //The return address follows the saved frame pointer (word-addressed).
    emit("ldr %d, %d, %d", rax, tmp, 1);
    pop(tmp);
}

/// @brief potato | Set the register class for parameter passing to RAX. 0 is INTEGER, 1 is SSE, 2 is MEMORY.
/// @param node 
static void emit_builtin_reg_class(Node *node) {
    Node *arg = vec_get(node->args, 0);
    assert(arg->ty->kind == KIND_PTR);
    Type *ty = arg->ty->ptr;
    if (ty->kind == KIND_STRUCT)
        emit("movi %d, %d", eax, 2);
    else if (is_flotype(ty))
        emit("movi %d, %d", eax, 1);
    else
        emit("movi %d, %d", eax, 0);
}

// -- 9/5/26 start --

/// @brief potato | emit: copy variables into assembly registers
/// @param node 
static void emit_builtin_va_start(Node *node) {
    SAVE;
    assert(vec_len(node->args) == 1);
    emit_expr(vec_head(node->args));
    push(rcx);
    //store the number of general-purpose variables (2 bytes each)
    //emit("movl $%d, (#rax)", numgp * 8);
    emit_asm(ins_movi, rcx, numgp * 2);
    emit_asm(ins_str, rcx, rax, 0);

    //store the number of floating-point variables

    /*
     important note: there is no FP support in the current implementation,
     which is why this stores only 2-byte long data instead of full 16-byte floats
    */
    //emit("movl $%d, 4(#rax)", 48 + numfp * 16);
    emit_asm(ins_movi, rcx, 48 + numfp * 2);
    emit_asm(ins_str, rcx, rax, 2);

    //calculate the address of the register save area
    //emit("lea %d(#rbp), #rcx", -REGAREA_SIZE);
    emit("addi %d, %d, %d", rcx, rbp, -REGAREA_SIZE);

    //store the address of the register save area
    //emit("mov #rcx, 16(#rax)");
    emit_asm(ins_str, rcx, rax, 8);
    pop(rcx);
}

/*
    --FUNCTION CALLING--
The ISA does not have as many usable registers as x86 does, so
function arguments will have to change. there are 6 registers that can be
used for GP-length arguments, combined with 2 registers for FP-length arguments.
the caller will save rax, rcx, rdx, xmm0, xmm1, and tmp before jumping.
*/

/// @brief potato | emit the builtin parameters 
/// of a function (return address, register class, and variables)
/// @param node 
/// @return 
static bool maybe_emit_builtin(Node *node) {
    SAVE;
    if (!strcmp("__builtin_return_address", node->fname)) {
        emit_builtin_return_address(node);
        return true;
    }
    if (!strcmp("__builtin_reg_class", node->fname)) {
        emit_builtin_reg_class(node);
        return true;
    }
    if (!strcmp("__builtin_va_start", node->fname)) {
        emit_builtin_va_start(node);
        return true;
    }
    return false;
}

// --9/7/26 start --

/// @brief potato | sort function inputs into registers and push overflow
/// @param ints 
/// @param floats 
/// @param rest 
/// @param args 
static void classify_args(Vector *ints, Vector *floats, Vector *rest, Vector *args) {
    SAVE;
    //start at 0 input arguments
    int ireg = 0, xreg = 0;
    //6 gp registers and 2 fp registers available.
    int imax = 6, xmax = 2;
    //traverse down the vector of input arguments
    for (int i = 0; i < vec_len(args); i++) {
        //we vectorize up to 6 GP registers and 2 FP registers.
        //anything more must get pushed to stack
        Node *v = vec_get(args, i);
        if (v->ty->kind == KIND_STRUCT) {
            //structs always go on the stack
            //since they're too large for registers
            vec_push(rest, v);
        } else if (is_flotype(v->ty)) {
            //add FP context to vector
            vec_push((xreg++ < xmax) ? floats : rest, v);
        } else {
            //add GP context to vector
            vec_push((ireg++ < imax) ? ints : rest, v);
        }
    }
}

/// @brief potato | save function register arguments to stack
/// @param nints 
/// @param nfloats 
static void save_arg_regs(int nints, int nfloats) {
    SAVE;
    //make sure there are not more argument registers than available
    assert(nints <= 6);
    assert(nfloats <= 2);
    for (int i = 0; i < nints; i++)
        //push the input argument regs we have filled, ascending
        push(REGS[i]);
    for (int i = 1; i < nfloats; i++)
        //push whatever fp regs are filled, ascending
        push_xmm(i);
}

/// @brief potato | restore function argument registers from stack
/// @param nints 
/// @param nfloats 
static void restore_arg_regs(int nints, int nfloats) {
    SAVE;
    for (int i = nfloats - 1; i > 0; i--)
        pop_xmm(i);
    for (int i = nints - 1; i >= 0; i--)
        pop(REGS[i]);
}

/// @brief potato | emit: push all function arguments to the stack
/// @param vals 
/// @return 
static int emit_args(Vector *vals) {
    SAVE;
    //size of arguments pushed to the stack
    int r = 0;
    //traverse argument vector
    for (int i = 0; i < vec_len(vals); i++) {
        //make a new node of the vector at index i
        Node *v = vec_get(vals, i);
        //if struct, push address and 
        if (v->ty->kind == KIND_STRUCT) {
            //get the address of the struct
            emit_addr(v);
            //then push the whole struct to the stack
            r += push_struct(v->ty->size) / EX_ISA_REG_BYTES + 1;
        //for floats, push the floating-point argument registers
        } else if (is_flotype(v->ty)) {
            //fetch the agrument and pull it to a register
            emit_expr(v);
            //then push to stack
            push_xmm(0);
            //each FP will be 4 bytes (2 addresses in word-addressed memory)
            r += 2;
        //other types 
        } else {
            //fetch into register
            emit_expr(v);
            emit_normalize_int(v->ty);
            //then push to stack
            push(rax);
            r += 1;
        }
    }
    return r;
}

/// @brief potato | pop n integer arguments from stack
/// @param nints 
static void pop_int_args(int nints) {
    SAVE;
    for (int i = nints - 1; i >= 0; i--)
        pop(REGS[i]);
}

/// @brief potato | pop n floating-point arguments from stack
/// @param nfloats 
static void pop_float_args(int nfloats) {
    SAVE;
    for (int i = nfloats - 1; i >= 0; i--)
        pop_xmm(i);
}

/// @brief emit: convert return data to boolean
/// @param ty 
static void maybe_booleanize_retval(Type *ty) {
    // if the return type is boolean, convert the return value to 0 or 1
    //rax is the default register for return data, so
    //a boolean (pass/fail) goes there for us to examine.
    //since there's no shorter registers to move or convert from, 
    //we could just stall a cycle lol but
    if (ty->kind == KIND_BOOL) {
        //emit("mov %d, %d", rax, rax);
    }
}

/*
    --FUNCTION CALLING--
The ISA does not have as many usable registers as x86 does, so
function arguments will have to change. there are 6 registers that can be
used for GP-length arguments, combined with 2 registers for FP-length arguments.
the caller will sort and save as many registers as the function needs before calling.
then the 
*/

/// @brief potato | emit: call function: the big kahuna
/// @param node 
static void emit_func_call(Node *node) {
    SAVE;
    int opos = stackpos;
    bool isptr = (node->kind == AST_FUNCPTR_CALL);
    Type *ftype = isptr ? node->fptr->ty->ptr : node->ftype;

    //make buckets for data types
    Vector *ints = make_vector();
    Vector *floats = make_vector();
    Vector *rest = make_vector();
    classify_args(ints, floats, rest, node->args);
    save_arg_regs(vec_len(ints), vec_len(floats));

    // if the stack is unaligned(which won't happen since we
    // use globally fixed word alignment, but FP support may change that),
    // then add apdding to the stack pointer.
    bool padding = 0; //stackpos % 16;
    if (padding) {
        emit("subi %d, %d", sp, 1);
        stackpos += 1;
    }

    //calculate size and push overflow arguments to the stack
    int restsize = emit_args(vec_reverse(rest));
    if (isptr) {
        emit_expr(node->fptr);
        push(rax);
    }

    //initialize the integer arguments into the stack
    emit_args(ints);
    //initialize the float arguments into the stack
    emit_args(floats);
    //pop the float args from the stack
    pop_float_args(vec_len(floats));
    //pop the integer args from the stack
    pop_int_args(vec_len(ints));

    //if the function is a function pointer, pop the address into r11
    if (isptr) pop(tmp);
    if (ftype->hasva)
        emit("movi %d %d", eax, vec_len(floats));

    //if the function is a function pointer, jump to the function
    if (isptr) {
        push(pc);
        stackpos -= 1;
        emit("jmp %d", tmp);
    }
    else {
        //if the function is not a pointer,
        //we jump to the function name instead.
        push(pc);
        stackpos -= 1;
        emit("jmp %s", node->fname);
        maybe_booleanize_retval(node->ty);
    }
    if (restsize > 0) {
        //if there are arguments on the stack,
        //we remove them from the stack and restore the
        //original stack position.
        emit_asm(ins_addi, sp, sp, restsize);
        stackpos -= restsize;
    }
    
    //if there was padding, we undo it.
    if (padding) {
        emit("addi %d, %d", stackpos, 1);
        stackpos -= 1;
    }

    //then we restore the argument registers.
    restore_arg_regs(vec_len(ints), vec_len(floats));
    emit_normalize_int(node->ty);
    //and ensure the stack has returned to its original position.
    fprintf(stderr, "stackpos: %d, opos: %d\n", stackpos, opos);
    assert(opos == stackpos);
}

/// @brief potato | emit: declare variable
/// @param node 
static void emit_decl(Node *node) {
    SAVE;
    if (!node->declinit)
        return;
    emit_decl_init(node->declinit, node->declvar->loff, node->declvar->ty->size);
}

/// @brief potato | emit: convert expression between types
/// @param node 
static void emit_conv(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_load_convert(node->ty, node->operand->ty);
}

/// @brief potato | emit: dereference a variable pointer
/// @param node 
static void emit_deref(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_lload(node->operand->ty->ptr, rax, 0);
    emit_load_convert(node->ty, node->operand->ty->ptr);
}

/// @brief potato | emit: ternary statement
/// @param node 
static void emit_ternary(Node *node) {
    SAVE;
    emit_expr(node->cond);
    char *ne = make_label();
    emit_je(ne);
    if (node->then)
        emit_expr(node->then);
    if (node->els) {
        char *end = make_label();
        emit_jmp(end);
        emit_label(ne);
        emit_expr(node->els);
        emit_label(end);
    } else {
        emit_label(ne);
    }
}

/// @brief potato | emit: jump statement
/// @param node 
static void emit_goto(Node *node) {
    SAVE;
    assert(node->newlabel);
    emit_jmp(node->newlabel);
}

/// @brief potato | emit: return statement
/// @param node 
static void emit_return(Node *node) {
    SAVE;
    if (node->retval) {
        emit_expr(node->retval);
        maybe_booleanize_retval(node->retval->ty);
        emit_normalize_int(node->retval->ty);
    }
    emit_ret();
}

/// @brief potato | emit: compound statement
/// @param node 
static void emit_compound_stmt(Node *node) {
    SAVE;
    for (int i = 0; i < vec_len(node->stmts); i++)
        emit_expr(vec_get(node->stmts, i));
}


//TODO: check if the test instructions execute the correct
//underlying logical comparison


/// @brief emit: short circuit logical AND(&&) operation
/// @param node 
static void emit_logand(Node *node) {
    SAVE;
    //this function is useful because it skips the second operand if
    //the first node evaluates to zero.
    char *end = make_label();
    emit_expr(node->left);
    //logical and

    emit("cmp %d, %d", rax, rax);
    emit("mov %d, %d", rax, zero);
    emit("je %s", end);

    emit_expr(node->right);

    emit("cmp %d, %d", rax, rax);
    emit("mov %d, %d", rax, zero);
    emit("je %s", end);
    emit("movi %d, %d", rax, 1);

    emit_label(end);
}

/// @brief emit: logical OR operation
/// @param node 
static void emit_logor(Node *node) {
    SAVE;
    char *end = make_label();
    emit_expr(node->left);

    emit("cmp %d, %d", rax, rax);
    emit("movi, rax, 1");
    emit("jne %s", end);

    emit_expr(node->right);

    emit("cmp %d, %d", rax, rax);
    emit("movi, rax, 1");
    emit("jne %s", end);
    emit("mov %d, %d", rax, zero);

    emit_label(end);
}

// -- 9/10/26 start --

/// @brief potato | emit: logical NOT operation
/// @param node 
static void emit_lognot(Node *node) {
    SAVE;
    emit_expr(node->operand);
    //copy the x86 assembly, but in EX_ISA
    emit("ins_cmp %d %d", rax, zero);
    emit("ins_seteq %d", rax);
}

/// @brief potato | emit: bitwise AND operation
/// @param node 
static void emit_bitand(Node *node) {
    SAVE;
    emit_expr(node->left);
    push(rax);
    emit_expr(node->right);
    pop(rcx);
    emit("and %d, %d, %d", rax, rcx, rax);
}


/// @brief potato | emit: bitwise OR operation
/// @param node 
static void emit_bitor(Node *node) {
    SAVE;
    emit_expr(node->left);
    push(rax);
    emit_expr(node->right);
    pop(rcx);
    emit("or %d, %d, %d", rax, rcx, rax);
}

/// @brief potato | emit: bitwise NOT operation
/// @param node 
static void emit_bitnot(Node *node) {
    SAVE;
    emit_expr(node->left);
    //we XOR rax with 0xFFFF to compute a bitwise NOT
    emit("movi %d, %d", tmp, 0xFFFF);
    emit("xor %d, %d, %d", rax, tmp, rax);
}

/// @brief potato | emit: castvariable type operation
/// @param node 
static void emit_cast(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit_load_convert(node->ty, node->operand->ty);
    return;
}

/// @brief potato | emit: comma operator 
/// @param node 
static void emit_comma(Node *node) {
    SAVE;
    emit_expr(node->left);
    emit_expr(node->right);
}

/// @brief potato | emit: assign variable value. this may have some problems later.
/// @param node 
static void emit_assign(Node *node) {
    SAVE;

    if (node->left->ty->kind == KIND_STRUCT &&
        node->left->ty->size > 1) {
        emit_copy_struct(node->left, node->right);
    } else {
        emit_expr(node->right);
        emit_load_convert(node->ty, node->right->ty);
        emit_store(node->left);
    }
}

/// @brief potato | emit: label address
/// @param node 
static void emit_label_addr(Node *node) {
    SAVE;
    emit("movi %d, %s ", rax, node->newlabel);
}

/// @brief potato | emit: goto statement dependent on computation
/// @param node 
static void emit_computed_goto(Node *node) {
    SAVE;
    emit_expr(node->operand);
    emit("jmp %d", rax);
}

/// @brief potato | emit: all expression types converted to assembly
/// @param node 
static void emit_expr(Node *node) {
    SAVE;
    maybe_print_source_loc(node);
    switch (node->kind) {
    case AST_LITERAL: emit_literal(node); return;
    case AST_LVAR:    emit_lvar(node); return;
    case AST_GVAR:    emit_gvar(node); return;
    case AST_FUNCDESG: emit_addr(node); return;
    case AST_FUNCALL:
        if (maybe_emit_builtin(node))
            return;
        // fall through
    case AST_FUNCPTR_CALL:
        emit_func_call(node);
        return;
    case AST_DECL:    emit_decl(node); return;
    case AST_CONV:    emit_conv(node); return;
    case AST_ADDR:    emit_addr(node->operand); return;
    case AST_DEREF:   emit_deref(node); return;
    case AST_IF:
    case AST_TERNARY:
        emit_ternary(node);
        return;
    case AST_GOTO:    emit_goto(node); return;
    case AST_LABEL:
        if (node->newlabel)
            emit_label(node->newlabel);
        return;
    case AST_RETURN:  emit_return(node); return;
    case AST_COMPOUND_STMT: emit_compound_stmt(node); return;
    case AST_STRUCT_REF:
        emit_load_struct_ref(node->struc, node->ty, 0);
        return;
    case OP_PRE_INC:   emit_pre_inc_dec(node, "add"); return;
    case OP_PRE_DEC:   emit_pre_inc_dec(node, "sub"); return;
    case OP_POST_INC:  emit_post_inc_dec(node, "add"); return;
    case OP_POST_DEC:  emit_post_inc_dec(node, "sub"); return;
    case '!': emit_lognot(node); return;
    case '&': emit_bitand(node); return;
    case '|': emit_bitor(node); return;
    case '~': emit_bitnot(node); return;
    case OP_LOGAND: emit_logand(node); return;
    case OP_LOGOR:  emit_logor(node); return;
    case OP_CAST:   emit_cast(node); return;
    case ',': emit_comma(node); return;
    case '=': emit_assign(node); return;
    case OP_LABEL_ADDR: emit_label_addr(node); return;
    case AST_COMPUTED_GOTO: emit_computed_goto(node); return;
    default:
        emit_binop(node);
    }
}

/// @brief potato | emit: zero register
/// @param size 
static void emit_zero(int size) {
    SAVE;
    //emit blank words until we meet the requested size.
    for(; size >= 1; size--) emit(".word 0");
}

/// @brief potato | emit: pad data variables
/// @param node 
/// @param off 
static void emit_padding(Node *node, int off) {
    SAVE;
    int diff = node->initoff - off;
    assert(diff >= 0);
    emit_zero(diff);
}

/// @brief  potato | emit: address of data or variable
/// @param operand 
/// @param depth 
static void emit_data_addr(Node *operand, int depth) {
    switch (operand->kind) {
    case AST_LVAR: {
        char *label = make_label();
        emit_label(label);
        do_emit_data(operand->lvarinit, operand->ty->size, 0, depth + 1);
        emit(".word %s", label);
        return;
    }
    case AST_GVAR:
        emit(".word %s", operand->glabel);
        return;
    default:
        error("internal error");
    }
}

/// @brief potato | emit: character pointer
/// @param s 
/// @param depth 
static void emit_data_charptr(char *s, int depth) {
    char *label = make_label();
    emit_label(label);
    emit(".string \"%s\"", quote_cstring(s));
    emit(".word %s", label);
}

/*
the actual emission of variables happens here.
since the current architecture does not consider floats, 
we simply use words for all data storage. booleans and chars
are extended to use words as well.
future changes can extend floats to use .long or .quad if the registers
are added.
*/

/// @brief potato | emit: declare primitive type in x86 assembly
/// @param ty 
/// @param val 
/// @param depth 
static void emit_data_primtype(Type *ty, Node *val, int depth) {
    switch (ty->kind) {
    case KIND_FLOAT: {
        float f = val->fval;
        emit(".word %d", *(uint32_t *)&f);
        break;
    }
    case KIND_DOUBLE:
        emit(".word %ld", *(uint64_t *)&val->fval);
        break;
    case KIND_BOOL:
        emit(".word %d", !!eval_intexpr(val, NULL));
        break;
    case KIND_CHAR:
        emit(".word %d", eval_intexpr(val, NULL));
        break;
    case KIND_SHORT:
        emit(".word %d", eval_intexpr(val, NULL));
        break;
    case KIND_INT:
        emit(".word %d", eval_intexpr(val, NULL));
        break;
    case KIND_LONG:
    case KIND_LLONG:
    case KIND_PTR:
        if (val->kind == OP_LABEL_ADDR) {
            emit(".word %s", val->newlabel);
            break;
        }
        bool is_char_ptr = (val->operand->ty->kind == KIND_ARRAY && val->operand->ty->ptr->kind == KIND_CHAR);
        if (is_char_ptr) {
            emit_data_charptr(val->operand->sval, depth);
        } else if (val->kind == AST_GVAR) {
            emit(".word %s", val->glabel);
        } else {
            Node *base = NULL;
            int v = eval_intexpr(val, &base);
            if (base == NULL) {
                emit(".word %u", v);
                break;
            }
            Type *ty = base->ty;
            if (base->kind == AST_CONV || base->kind == AST_ADDR)
                base = base->operand;
            if (base->kind != AST_GVAR)
                error("global variable expected, but got %s", node2s(base));
            assert(ty->ptr);
            emit(".word %s+%u", base->glabel, v * ty->ptr->size);
        }
        break;
    default:
        error("don't know how to handle\n  <%s>\n  <%s>", ty2s(ty), node2s(val));
    }
}

/// @brief potato | emit: directly emit data into memory
/// @param inits 
/// @param size 
/// @param off 
/// @param depth 
static void do_emit_data(Vector *inits, int size, int off, int depth) {
    SAVE;
    for (int i = 0; i < vec_len(inits) && 0 < size; i++) {
        Node *node = vec_get(inits, i);
        Node *v = node->initval;
        emit_padding(node, off);
        if (node->totype->bitsize > 0) {
            assert(node->totype->bitoff == 0);
            long data = eval_intexpr(v, NULL);
            Type *totype = node->totype;
            for (i++ ; i < vec_len(inits); i++) {
                node = vec_get(inits, i);
                if (node->totype->bitsize <= 0) {
                    break;
                }
                v = node->initval;
                totype = node->totype;
                data |= ((((long)1 << totype->bitsize) - 1) & eval_intexpr(v, NULL)) << totype->bitoff;
            }
            emit_data_primtype(totype, &(Node){ AST_LITERAL, totype, .ival = data }, depth);
            off += totype->size;
            size -= totype->size;
            if (i == vec_len(inits))
                break;
        } else {
            off += node->totype->size;
            size -= node->totype->size;
        }
        if (v->kind == AST_ADDR) {
            emit_data_addr(v->operand, depth);
            continue;
        }
        if (v->kind == AST_LVAR && v->lvarinit) {
            do_emit_data(v->lvarinit, v->ty->size, 0, depth);
            continue;
        }
        emit_data_primtype(node->totype, node->initval, depth);
    }
    emit_zero(size);
}

/// @brief potato | emit: emit initialized data into memory
/// @param v 
/// @param off 
/// @param depth 
static void emit_data(Node *v, int off, int depth) {
    SAVE;
    emit(".data");
    if (!v->declvar->ty->isstatic)
        emit_noindent(".global %s", v->declvar->glabel);
    emit_noindent("%s:", v->declvar->glabel);
    do_emit_data(v->declinit, v->declvar->ty->size, off, depth);
    emit_noindent(".text");
}


//in progress
//lcomm is complete. it will now allocate n words at m name in data

/// @brief potato | emit: emit uninitialized data space into memory
/// @param v 
static void emit_bss(Node *v) {
    SAVE;
    emit(".data");
    if (!v->declvar->ty->isstatic)
        emit(".global %s", v->declvar->glabel);
    emit(".lcomm %s, %d", v->declvar->glabel, v->declvar->ty->size);
    emit_noindent(".text");
}

/// @brief potato | emit: emit global variable
/// @param v 
static void emit_global_var(Node *v) {
    SAVE;
    if (v->declinit)
        emit_data(v, 0, 0);
    else
        emit_bss(v);
}

/// @brief potato | push register context to stack including vector registers
/// @return 
static int emit_regsave_area() {
    //set back the stack pointer by the size of the context window
    emit("sub $%d, #rsp", REGAREA_SIZE);
    //push GP registers
    emit("str %d, %d", rax, rsp, 0);
    emit("str %d, %d", rbx, rsp, 1);
    emit("str %d, %d", rcx, rsp, 2);
    emit("str %d, %d", rdx, rsp, 3);
    emit("str %d, %d", rsi, rsp, 4);
    emit("str %d, %d", rdi, rsp, 5);
    /*
    emit("mov #rdi, (#rsp)");
    emit("mov #rsi, 8(#rsp)");
    emit("mov #rdx, 16(#rsp)");
    emit("mov #rcx, 24(#rsp)");
    emit("mov #r8, 32(#rsp)");
    emit("mov #r9, 40(#rsp)");
    emit("movaps #xmm0, 48(#rsp)");
    emit("movaps #xmm1, 64(#rsp)");
    emit("movaps #xmm2, 80(#rsp)");
    emit("movaps #xmm3, 96(#rsp)");
    emit("movaps #xmm4, 112(#rsp)");
    emit("movaps #xmm5, 128(#rsp)");
    emit("movaps #xmm6, 144(#rsp)");
    emit("movaps #xmm7, 160(#rsp)");
    */
    //push FP registers
    emit("str %d, %d", xmm0, rsp, 6);
    emit("str %d, %d", xmm1, rsp, 7);
    return REGAREA_SIZE;
}

/// @brief potato | emit: push all function parameters to stack
/// @param params 
/// @param off 
static void push_func_params(Vector *params, int off) {
    int ireg = 0;
    int xreg = 0;
    int arg = 2;
    for (int i = 0; i < vec_len(params); i++) {
        Node *v = vec_get(params, i);
        if (v->ty->kind == KIND_STRUCT) {
            //emit("lea %d(#rbp), #rax", arg * 8);
            emit("addi %d, %d, %d", rax, rbp, arg);
            int size = push_struct(v->ty->size);
            off -= size;
            //we're word addressed, so we increment everything by words.
            arg += size;
        } else if (is_flotype(v->ty)) {
            if (xreg >= 2) {
                //for local variables beyond our register count,
                //we store the floating point argument
                //to the base pointer plus argument offset
                //emit("mov %d(#rbp), #rax", arg++ * 8);
                emit("str %d, %d, %d", rax, rbp, arg++);
                push(rax);
            } else {
                //otherwise we just push the xmm registers
                push_xmm(xreg++);
            }
            off -= 1;
        } else {
            if (ireg >= 6) {
                //if we need to store a boolean in overflow,
                if (v->ty->kind == KIND_BOOL) {
                    //we just store it lol because we have no byteregs
                    //emit("mov %d(#rbp), #al", arg++ * 8);
                    //emit("movzb #al, #eax");
                    emit("str %d, %d, %d", rax, rbp, arg++);
                } else {
                    //emit("mov %d(#rbp), #rax", arg++ * 8);
                    //normal stuff works the same.
                    emit("str %d, %d, %d", rax, rbp, arg++);
                }
                push(rax);
            } else {
                //for things we have registers for, just move to
                //the proper register (unneeded)
                //if (v->ty->kind == KIND_BOOL)
                    //emit("movzb #%s, #%s", SREGS[ireg], MREGS[ireg]);
                push(REGS[ireg++]);
            }
            off -= 1;
        }
        v->loff = off;
    }
}

/// @brief  potato | emit: push context to stack and prepare registers for new function
/// @param func 
static void emit_func_prologue(Node *func) {
    SAVE;
    emit(".text");
    if (!func->ty->isstatic)
        emit_noindent(".global %s", func->fname);
    emit_noindent("%s:", func->fname);
    emit("nop");
    push(rbp);
    //emit("mov #rsp, #rbp");
    emit("str %d, %d, %d", rsp, rbp, 0);
    int off = 0;
    if (func->ty->hasva) {
        set_reg_nums(func->params);
        off -= emit_regsave_area();
    }
    push_func_params(func->params, off);
    off -= vec_len(func->params);

    int localarea = 0;
    for (int i = 0; i < vec_len(func->localvars); i++) {
        Node *v = vec_get(func->localvars, i);
        int size = align(v->ty->size, 1);
        assert(size % 1 == 0);
        off -= size;
        v->loff = off;
        localarea += size;
    }
    if (localarea) {
        //emit("sub $%d, #rsp", localarea);
        emit("subi %d, %d, %d", rsp, rbp, localarea);
        stackpos += localarea;
    }
}

/// @brief potato | emit: emit function toplevel into memory
/// @param v 
void emit_toplevel(Node *v) {
    stackpos = 1;
    if (v->kind == AST_FUNC) {
        emit_func_prologue(v);
        emit_expr(v->body);
        emit_ret();
        emit("halt");
    } else if (v->kind == AST_DECL) {
        emit_global_var(v);
    } else {
        error("internal error");
    }
}

// ============================================================================
// Target abstraction interface (EX_ISA backend)
// ============================================================================

void gen_ex_isa_init(FILE *fp) {
    set_output_file(fp);
}

void gen_ex_isa_finalize(void) {
    close_output_file();
}

void gen_ex_isa_emit_toplevel(Node *v) {
    emit_toplevel(v);
}

void gen_ex_isa_set_output_file(FILE *fp) {
    set_output_file(fp);
}