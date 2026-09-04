// EX_ISA backend for 8cc compiler
// Target: 4-bit opcodes, 16 registers, 16-bit words, 256-byte address space
//
// Memory layout:
//   0x00-0xFF: code (256 bytes ROM)
//   0x00-0xBF: global data (64 bytes)
//   0xC0-0xFF: stack (64 bytes, grows downward from 0xFF)

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

    - Registers: R0 .. R15 (case-insensitive) or numeric 0..15. R0 is fixed at zero, R14 is TMP, and R15 is PC.
      -- Assembly formatting instructions --
      - Use .text for instructions and instruction labels.
      - Use .data for data directives and data labels.
      - Labels end with ':' and may appear on their own line.
      - Tokens are separated by whitespace and/or commas.
      - Comments may start with ';', '//' or '#'.
    - Registers are R0..R15 (case-insensitive).
    - Control-flow labels (JMP/JLT label form) must be .text labels.
      - Memory-address labels (STR/LDR label form) must be .data labels.

    .data directives (written to the separate data output/MIF described above):
      .word v1[, v2...]   writes one 16-bit word per value
      .space count        writes count zero words
      .long v1[, v2...]   writes 2 words per 32-bit value, most-significant word first
      .quad v1[, v2...]   writes 4 words per 64-bit value, most-significant word first
      .string "literal"   writes ceil((chars+1)/2) words, packing 2 chars/word (first char in the high
                          byte) plus a null terminator; backslash escapes (\n \t \r \\ \" \0) decode
                          to their real byte value before packing

    Instruction formats implemented :

      STR rA, rB/LABEL, soff (store RF[rA] -> D[RF[rB] + soff])
          -> 0001 raaa rbbb soff  pseudo-ins variant accepts 16-bit word offset using pseudoinstruction
      LDR rA, rB/LABEL, soff (load D[RF[rB] + soff] -> RF[rA])
          -> 0010 raaa rbbb soff  pseudo-ins variant accepts 16-bit word offset using pseudoinstruction

      ADD rA, rB, rC (rA = rB + rC)
          -> 0011 raaa rbbb rccc
      SUB rA, rB, rC (rA = rB - rC)
          -> 0100 raaa rbbb rccc
      HLT                              
          -> 0101 0000 0000 0000

    MOVI rA, imm8_or_label (rA = rA | imm)
          -> 0110 raaa dddddddd     (ORs the immediate value into the selected register, using pseudoins for >8 bits)
             can load a label from either iram or dram.
      OR  rA, rB, rC   (rA = rB | rC)
          -> 0111 raaa rbbb rccc
      AND rA, rB, rC   (rA = rB & rC)
          -> 1000 raaa rbbb rccc

      JMP offset/LABEL (PC = PC + soff12)
          -> 1001 bbbb bbbb bbbb  (signed 12-bit PC-relative offset)
      JMP rA (optional pseudo-form: PC = RF[rA])
          -> copies the register value into the PC register
      JNZ rA, rB, soff4 (PC = RF[rB] + soff4 if RF[rA] != 0)
          -> 1010 raaa rbbb bbbb
      JLT rA, rB, offset (PC = PC + offset if rA < rB)
          -> 1011 raaa rbbb bbbb    (4-bit signed offset relative to next instr)

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
      SHR rA, rB, shft (rA = rB >> shft)
          -> 0000 raaa shft rccc

      NOP                         
          -> 1000 0000 0000 0000   (AND R0 with R0 into R0, effectively a NOP)
      MOV rA, rB       (rA = rB)
          -> 1000 raaa rbbb rccc    (AND RA with RA into RB, effectively moving) 
      XOR rA, rB, rC   (rA = rB ^ rC)
          -> pseudo-ins


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
#define ins_nop 0xE
#define ins_mov 0xF
#define ins_xor 0x10

//dedicated registers for stack and frame pointers
#define pc 15       //program counter register
#define asm_tmp 14  //temp register exclusively for assembly to machine code translation. don't use
#define tmp 13      //temp register for extended c translation
#define sp 12       //stack pointer register
#define fp 11       //frame pointer register
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
#define rbp fp  //x86 base pointer register equivalent
#define rsp sp  //x86 stack pointer register equivalent

#define xmm0 8  //x86 floating point register equivalent (TEMP)
#define xmm1 9  //x86 floating point register equivalent (TEMP)

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
static char *REGS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static char *SREGS[] = {"dil", "sil", "dl", "cl", "r8b", "r9b"};
static char *MREGS[] = {"edi", "esi", "edx", "ecx", "r8d", "r9d"};
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


/// @brief overload: emits an assembly instruction bypassing the emitf format
/// @param op 
/// @param a 
static void emit_asm(int op, int a) {
    switch (op) {
    case ins_hlt:
        emit("hlt");
        return;
    case ins_nop:
        emit("nop");
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
    case ins_mov:
        emit("mov %d, %d", a, b);
        return;
    case ins_jmp:
        emit("jmp %d", a);
        return;
    case ins_jnz:
        emit("jnz %d, %d, %d", a, b, 0);
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
    case ins_xor:
        emit("xor %d, %d, %d", a, b, c);
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
static int get_int_reg(Type *ty, char r) {
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
static int get_load_inst(Type *ty) {
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
    emit_asm(ins_movi, tmp, 1);
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
    emit_asm(ins_movi, tmp, 1);
    emit_asm(ins_add, sp, tmp, sp);
    stackpos -= 1;
    assert(stackpos >= 0);
}

// -- 7/17/26 start --

/// @brief potato | emits: push a register to the global stack, update sp
/// @param reg 
static void push(int reg) {
    SAVE;
    emit("movi %d, %d", tmp, 1);
    emit("sub %d, %d, %d", sp, tmp, sp);
    emit("str %d, %d, %d", reg, sp, 0);
    stackpos += 1; //remember, each instruction is 16 bits, so only 1 word
}

/// @brief potato | emits: pop a register from the global stack, update sp
/// @param reg 
static void pop(int reg) {
    SAVE;
    emit("ldr %d, %d, %d", reg, sp, 0);
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
    emit_asm(ins_shr, rax, rax, ty->bitoff);
    push(rcx);
    emit_asm(ins_movi, rcx, (1 << (long)ty->bitsize) - 1);
    //emit("mov $0x%lx, #rcx", (1 << (long)ty->bitsize) - 1);
    //emit("and #rcx, #rax");
    emit_asm(ins_and, rax, rcx, rax);
    pop(rcx);
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
    push(rdi);

    //operation: rax(minus last bit) << bitoff ||  rcx(last bit) << bitoff

    //mask out last bit of rax
    //emit("mov $0x%lx, #rdi", (1 << (long)ty->bitsize) - 1);
    emit_asm(ins_movi, rdi, (1 << (long)ty->bitsize) - 1);
    emit_asm(ins_and, rdi, rax, rax);
    //adjust for offset
    emit_asm(ins_shl, rax, rax, ty->bitoff);
    //mask out last bit of rcx 
    emit_asm(ins_movi, rdi, ~(((1 << (long)ty->bitsize) - 1) << ty->bitoff) & 0xFFFF);
    emit_asm(ins_and, rdi, rcx, rcx);
    emit_asm(ins_or, rcx, rax, rax);
    pop(rdi);
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
            // The assembler may use TMP to expand a 16-bit label address;
            // clear it before loading the array-element displacement.
            emit_asm(ins_mov, tmp, zero);
            // Put the requested byte/word displacement in TMP.
            emit_asm(ins_movi, tmp, off);
            // Form label + off so rax holds the address of the array element.
            emit_asm(ins_add, rax, rax, tmp);
        }
        return;
    }

    // Load the scalar value at data label + off directly into rax. The
    // assembler resolves the data label and expands the address load as needed.
    emit("ldr %d, %s, %d", rax, label, off);
    // Extract the requested bit-field after its containing word is loaded.
    maybe_emit_bitshift_load(ty);

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
    switch(ty->kind) {
    case KIND_BOOL:
    case KIND_CHAR:
        ty->usig ? emit_asm(ins_mov, rax, rax) : emit_asm(ins_mov, rax, rax);
        return;
    case KIND_SHORT:
        ty->usig ? emit_asm(ins_mov, rax, rax) : emit_asm(ins_mov, rax, rax);
        return;
    case KIND_INT:
        ty->usig ? emit_asm(ins_mov, rax, rax) : emit_asm(ins_mov, rax, rax);
        return;
    //no support in isa
    case KIND_LONG:
    case KIND_LLONG:
        return;
    }
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
        emit_asm(ins_movi, tmp, off < 0 ? -off : off);
        if (off < 0) {
            // Subtract the negative displacement to form base + off in rax.
            emit_asm(ins_sub, rax, rax, tmp);
        } else {
            // Add the positive displacement to form base + off in rax.
            emit_asm(ins_add, rax, rax, tmp);
        }
        return;
    }

    // LDR's assembler pseudo-instruction accepts the full signed 16-bit
    // displacement and lowers base + off to the necessary instruction sequence.
    emit_asm(ins_ldr, rax, base, off);

    // Extract the requested bit-field after loading its containing word.
    maybe_emit_bitshift_load(ty);

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
    //emit("mov (#rsp), #rcx");
    emit("STR %d, %d, 0", rcx, rsp);

    char *reg = get_int_reg(ty, 'c');
    if (off)
        //emit("mov #%s, %d(#rax)", reg, off);
        emit("STR %d, %d, %d", reg, rax, off);
    else
        emit("STR %d, %d, 0", reg, rax);
    pop(rax);
}

/// @brief potato | emit: helper: push rax, deref pointer with offset 0
/// @param var 
static void emit_assign_deref(Node *var) {
    SAVE;
    push("rax");
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
        emit("MOVI %d, %d", tmp, size);
        emit("MULT %d, %d, %d", rax, rax, tmp);
    }
    emit("MOV %d, %d, %d", rax, rax, rcx);
    pop(rax);
    switch (kind) {
    case '+': emit("ADD %d, %d, %d", rcx, rax, rax); break;
    case '-': emit("SUB %d, %d, %d", rcx, rax, rax); break;
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
    for (; start <= end - 4; start += 4)
        //emit("movl $0, %d(#rbp)", start);
        emit("STR %d, %d, %d", zero, rbp, start);
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
        emit("cmp $0, #rax");
        emit("setne %d", rax);
    }
    emit("mov %d, %d", rax, eax);
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
        int kind = node->left->ty->kind;
        if (kind == KIND_LONG || kind == KIND_LLONG)
          emit("cmp rax, rcx");
        else
          emit("cmp eax, ecx");
    }
    if (is_flotype(node->left->ty) || node->left->ty->usig)
        emit("%s rax", usiginst);
    else
        emit("%s rax", inst);
    emit("mov rax, eax");
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
    case OP_SAL: op = "sal"; break;
    case OP_SAR: op = "sar"; break;
    case OP_SHR: op = "shr"; break;
    case '/': case '%': break;
    default: error("invalid operator '%d'", node->kind);
    }
    emit_expr(node->left);
    push(rax);
    emit_expr(node->right);
    emit("mov rax, rcx");
    pop(rax);
    if (node->kind == '/' || node->kind == '%') {
        if (node->ty->usig) {
          //emit("xor #edx, #edx");
          //emit("div #rcx");
          error("unsupported operation '%d'", node->kind);
        } else {
          //emit("cqto");
          //emit("idiv #rcx");
          error("unsupported operation '%d'", node->kind);
        }
        if (node->kind == '%')
            error("unsupported operation '%d'", node->kind);
            //emit("mov #edx, #eax");
    } else if (node->kind == OP_SAL || node->kind == OP_SAR || node->kind == OP_SHR) {
        //emit("%s #cl, #%s", op, get_int_reg(node->left->ty, 'a'));
        emit("%s %d, %d", op, get_int_reg(node->left->ty, 'a'), rcx);
    } else {
        emit("%s %d, %d", op, rax, rcx);
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
    error("floating point arithmetic is not supported in this ISA");
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
        emit_intcast(from);
    else if (is_inttype(to))
        emit_toint(from);
}

/*
in the ISA, there's no assembly leave or return instruction.
we just have to manually pop the stack and return.
*/

/// @brief potato | emit: return call
static void emit_ret() {
    SAVE;
    emit("leave");
    emit("ret");
    //set stack pointer to the current base pointer,
    //then pop the old bsae pointer from the stack to restore it.
    emit("mov rsp, rbp");
    pop(rbp);

    //return

    pop(pc);
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
        emit("STR %d, %d, %d", !!node->ival, rbp, off);
        break;
    }  
    //chars are also stored as a byte
    case KIND_CHAR:{
        //emit("movb $%d, %d(#rbp)", node->ival, off);
        emit("STR %d, %d, %d", node->ival, rbp, off);
        
        break;
    }
    //shorts are stored as a half-word. (technically that's all we have)
    case KIND_SHORT: {
        //emit("movw $%d, %d(#rbp)", node->ival, off);
        emit("STR %d, %d, %d", node->ival, rbp, off);
        break;
    }
    //ints are stored as a lower word (32 bits). we still store the whole 16-bit register.
    case KIND_INT: {
        //emit("movl $%d, %d(#rbp)", node->ival, off);
        emit("STR %d, %d, %d", node->ival, rbp, off);
        break;
    }
    //longs, long longs and pointers are stored as two words, ie 64 bits split into two 32-bit words.
    //our memory is word-addressed, so we offset by one address instead of 4 bytes.
    case KIND_LONG:
    case KIND_LLONG:
    case KIND_PTR: {
        //emit("movl $%lu, %d(#rbp)", ((uint64_t)node->ival) & ((1L << 32) - 1), off);
        //emit("movl $%lu, %d(#rbp)", ((uint64_t)node->ival) >> 32, off + 4);
        emit("STR %d, %d, %d", ((uint64_t)node->ival) & ((1L << 32) - 1), rbp, off);
        emit("STR %d, %d, %d", ((uint64_t)node->ival) >> 32, rbp, off + 1);
        break;
    }
    //floats are stored as one word.
    //they're first cast to a uint32, then stored.
    //yes, this doesn't fit into our memory cleanly, but this will work for the moment
    //and will be addressed when we implement floating point support in the ISA.
    case KIND_FLOAT: {
        float fval = node->fval;
        //emit("movl $%u, %d(#rbp)", *(uint32_t *)&fval, off);
        emit("STR %d, %d, %d", *(uint32_t *)&fval, rbp, off);
        break;
    }
    //doubles are stored similarly, but as two words through a uint64.
    //our memory is word-addressed, so we offset by one address instead of 4 bytes.
    case KIND_DOUBLE:
    case KIND_LDOUBLE: {
        //emit("movl $%lu, %d(#rbp)", *(uint64_t *)&node->fval & ((1L << 32) - 1), off);
        //emit("movl $%lu, %d(#rbp)", *(uint64_t *)&node->fval >> 32, off + 4);
        emit("STR %d, %d, %d", *(uint64_t *)&node->fval & ((1L << 32) - 1), rbp, off);
        emit("STR %d, %d, %d", *(uint64_t *)&node->fval >> 32, rbp, off + 1);
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

        //zero out the register
        emit("mov %d, %d", rax, zero);
        //insert the offset
        emit("movi %d, %d", rax, node->loff);
        //add the offset to the base
        emit("add %d, %d, %d", rax, rbp, rax);
        break;
    case AST_GVAR:
        //global variables are stored elsewhere, so we
        //use the global label and the instruction pointer to
        //find the absolute address.
        //emit("lea %s(#rip), #rax", node->glabel);

        //zero out the register
        emit("mov %d, %d", rax, zero);
        //insert the offset
        emit("movi %d, %s", rax, node->glabel);
        //add to the instruction pointer
        emit("add %d, %d, %d", rax, pc, rax);
        break;
    case AST_DEREF:
        //if we want to dereference a pointer,
        //we emit the related expression to get the address
        emit_expr(node->operand);
        break;
    case AST_STRUCT_REF:
        emit_addr(node->struc);
        //emit("add $%d, #rax", node->ty->offset);
        emit("movi %d, %d", tmp, node->ty->offset);
        emit("add %d, %d, %d", rax, tmp, rax);
        break;
    case AST_FUNCDESG:
        //emit("lea %s(#rip), #rax", node->fname);

        //lea pseudo-op
        emit("mov %d, %d", rax, zero);
        emit("movi %d, %s", rax, node->fname);
        emit("add %d, %d, %d", rax, pc, rax);
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
    push(tmp);
    emit_addr(right);
    emit("mov %d, %d", rcx, rax);
    emit_addr(left);
    int i = 0;
    for (; i < left->ty->size; i += 1) {
        //move 2 bytes at a time from rcx to rax
        //emit("movq %d(#rcx), #r11", i);
        //emit("movq #r11, %d(#rax)", i);
        
        //load data from rcx + i into temp, then 
        //copy to the destination address in rax + i
        emit("ldr %d, %d, %d", tmp, rcx, i);
        emit("str %d, %d, %d", tmp, rax, i);

    }

    //the ISA only uses 16-bit datasizes, so only one loop is needed.

    /*
    //repeat for 4 bytes
    for (; i < left->ty->size; i += 4) {
        emit("movl %d(#rcx), #r11", i);
        emit("movl #r11, %d(#rax)", i);
    }
    //repeat for single bytes
    for (; i < left->ty->size; i++) {
        emit("movb %d(#rcx), #r11", i);
        emit("movb #r11, %d(#rax)", i);
    }
    */
    pop(tmp);
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

/// @brief potato | emit: test then jump to label on equal
/// @param label 
static void emit_je(char *label) {
    //emit("test #rax, #rax");
    //set the bits in the status register, then seteq and jnz, in 
    //absence of je.
    push(tmp);
    emit("cmp %d, %d", rax, zero);
    //emit("je %s", label);
    emit("seteq %d", rax);
    emit("jnz %d, %s", rax, label);
    pop(tmp);
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

        //zero out the register
        emit("mov %d, %d", rax, zero);
        //insert the offset
        emit("movi %d, %s", rax, node->slabel);
        //add to the instruction pointer
        emit("add %d, %d, %d", rax, pc, rax);
        break;
    }
    default:
        error("internal error");
    }
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