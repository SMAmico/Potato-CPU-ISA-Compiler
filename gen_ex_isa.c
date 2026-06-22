// EX_ISA backend for 8cc compiler
// Target: 4-bit opcodes, 16 registers, 16-bit words, 256-byte address space
//
// Memory layout:
//   0x00-0x7F: code (128 bytes)
//   0x80-0xBF: global data (64 bytes)
//   0xC0-0xFF: stack (64 bytes, grows downward from 0xFF)
//
// Calling convention:
//   Arguments:     R1-R4 for first 4 integers, rest on stack
//   Return value:  R0
//   Return addr:   R15 or stack
//   Clobbered:     R1-R7, R15 (caller-saved)
//   Preserved:     R8-R14 (callee-saved)
//
// Register allocation (simplified):
//   R0:    return value
//   R1-R7: temp/argument registers (caller-saved)
//   R8-R14: preserved (callee-saved)
//   R15:   return address register

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"
#include "target.h"

// EX_ISA instruction opcodes (4-bit)
#define OP_NOP     0x0
#define OP_STR     0x1
#define OP_LDR     0x2
#define OP_ADD     0x3
#define OP_SUB     0x4
#define OP_HLT     0x5
#define OP_XOR     0x6
#define OP_OR      0x7
#define OP_AND     0x8
#define OP_JMP     0x9
#define OP_JNZ     0xA
#define OP_JLT     0xB
#define OP_SHL     0xC
#define OP_MULT    0xD
#define OP_SHR     0xE

// Target state
static FILE *outputfp = NULL;
static int code_offset = 0;      // Next available instruction address (0x00-0x7F)
static int data_offset = 0x80;   // Next available data address (0x80-0xBF)
static int stack_ptr = 0xFF;     // Current stack pointer (grows down from 0xFF)
static int label_count = 0;

// ============================================================================
// Label management for control flow
// ============================================================================

// Map label names to their EX_ISA addresses
// (Simplified implementation; full version would resolve forward/backward refs)
static Map *label_map = NULL;

// Register a label at current code offset
static void register_label(char *label) {
    if (!label_map)
        label_map = make_map();
    
    // Store label -> current code offset
    char *addr_str = malloc(16);
    snprintf(addr_str, 16, "0x%02x", code_offset);
    map_put(label_map, label, addr_str);
    
    fprintf(outputfp, ".%s:\n", label);
}

// Get address of a previously registered label (or return placeholder if not found)
static int get_label_addr(char *label) {
    if (!label_map)
        label_map = make_map();
    
    char *addr_str = map_get(label_map, label);
    if (addr_str) {
        return strtol(addr_str, NULL, 16);
    }
    // Not yet registered; return placeholder (would need forward reference resolution)
    return 0x00;
}

// Forward declarations for internal functions
static void emit_instr_hex(uint16_t instr);
static void emit_alu(int opcode, int ra, int rb, int rc);
static void emit_load(int addr, int reg);
static void emit_store(int reg, int addr);
static uint16_t make_instr(int opcode, int a, int b, int c);
static char *gen_ex_isa_make_label(void);
static void emit_jlt_instr(int addr, int r1, int r2);

// ============================================================================
// Backend initialization and finalization
// ============================================================================

void gen_ex_isa_init(FILE *fp) {
    outputfp = fp;
    code_offset = 0;
    data_offset = 0x80;
    stack_ptr = 0xFF;
    label_count = 0;
    if (!label_map)
        label_map = make_map();
}

void gen_ex_isa_finalize(void) {
    // Emit HLT to halt execution
    emit_instr_hex(make_instr(OP_HLT, 0, 0, 0));
    
    if (code_offset > 0x80) {
        fprintf(stderr, "warning: code section exceeds 128 bytes (offset=0x%02x)\n", code_offset);
    }
    if (data_offset > 0xC0) {
        fprintf(stderr, "warning: data section exceeds 64 bytes (offset=0x%02x)\n", data_offset);
    }
}

void gen_ex_isa_set_output_file(FILE *fp) {
    outputfp = fp;
}

// ============================================================================
// Instruction emission helpers
// ============================================================================

static void push_reg(int reg) {
    //store register at the stack register:

    //done by subtracting stack offset from stack pointer
    //then copying register data into stack pointer address

    //push data to stack
    emit_instr_hex(make_mem_instr(OP_STR, 1, stack_ptr));
    //update stack pointer
    stack_ptr--; 
    //blank out R1
    emit_instr_hex(make_instr(OP_XOR, 1, 1, 1));
    //set R1 to 1 if 
    
    emit_instr_hex(make_instr(OP_ADD, ));
}

// Emit a 16-bit instruction as hex
static void emit_instr_hex(uint16_t instr) {
    if (!outputfp) return;
    fprintf(outputfp, "%04X\n", instr & 0xFFFF);
    code_offset++;
    if (code_offset > 0x80) {
        fprintf(stderr, "error: code section overflow at address 0x%02x\n", code_offset);
    }
}

// Create a 3-register instruction: OPCODE RA RB RC
// Instruction format: [OPCODE(4bits)][RA(4bits)][RB(4bits)][RC(4bits)]
static uint16_t make_instr(int opcode, int a, int b, int c) {
    return ((opcode & 0xF) << 12) | ((a & 0xF) << 8) | ((b & 0xF) << 4) | (c & 0xF);
}

// Create a memory instruction: OPCODE REG ADDR
// For LDR: [OPCODE(4bits)][REG(4bits)][ADDR(8bits)]
// For STR: [OPCODE(4bits)][REG(4bits)][ADDR(8bits)]
static uint16_t make_mem_instr(int opcode, int reg, int addr) {
    if (opcode == OP_STR || opcode == OP_LDR)
        return ((opcode & 0xF) << 12) | ((reg & 0xF) << 8) | (addr & 0xFF);
    return 0;
}

// Create a jump instruction
// For JMP: [OPCODE(4)][0(4)][ADDR(8)]
// For JNZ: [OPCODE(4)][ADDR(8)][REG(4)]
static uint16_t make_jump_instr(int opcode, int field1, int field2) {
    if (opcode == OP_JMP)
        return ((opcode & 0xF) << 12) | (field2 & 0xFF);
    else if (opcode == OP_JNZ)
        return ((opcode & 0xF) << 12) | ((field1 & 0xFF) << 4) | (field2 & 0xF);
    return 0;
}

// Emit an ALU instruction (3-register format)
static void emit_alu(int opcode, int ra, int rb, int rc) {
    uint16_t instr = make_instr(opcode, ra, rb, rc);
    emit_instr_hex(instr);
}

// Emit a load instruction: LDR addr, Rr
static void emit_load(int addr, int reg) {
    uint16_t instr = make_mem_instr(OP_LDR, reg, addr);
    emit_instr_hex(instr);
}

// Emit a store instruction: STR Rr, addr
static void emit_store(int reg, int addr) {
    uint16_t instr = make_mem_instr(OP_STR, reg, addr);
    emit_instr_hex(instr);
}

// Emit an unconditional jump: JMP addr
static void emit_jmp(int addr) {
    uint16_t instr = make_jump_instr(OP_JMP, 0, addr);
    emit_instr_hex(instr);
}

// Emit a conditional jump (not-zero): JNZ addr, r
static void emit_jnz(int addr, int reg) {
    uint16_t instr = make_jump_instr(OP_JNZ, addr, reg);
    emit_instr_hex(instr);
}

static void emit_xor(int ra, int rb, int rc) {
    //get temp register
    emit_instr_hex(make_instr(OP_AND, ra, rb, rc));
    emit_instr_hex(make_instr(OP_))
}

// Generate a unique label
static char *gen_ex_isa_make_label(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), ".L%d", label_count++);
    return strdup(buf);
}

// ============================================================================
// AST code generation
// ============================================================================

// Forward declarations
static void emit_expr(Node *node);

// Temporary register allocator (simplified)
// Returns a temp register to use for intermediate values
// R1-R7 are caller-saved and can be used as temps
static int get_temp_reg(void) {
    static int temp_idx = 1;
    int reg = (temp_idx % 7) + 1;  // R1-R7
    temp_idx++;
    return reg;
}

// Emit a literal (constant) into R0
static void emit_literal(Node *node) {
    int val = node->ival;
    
    if (val == 0) {
        // R0 = 0: use SUB R0, R0, R0
        emit_alu(OP_SUB, 0, 0, 0);
    } else if (val == 1) {
        // R0 = 1: use ADD R0, zero, R0 where zero is pre-loaded... or use a temp
        // For now: allocate from data section
        int data_addr = data_offset;
        if (data_offset < 0xC0) data_offset += 2;
        emit_load(data_addr, 0);
    } else {
        // Load from data section
        int data_addr = data_offset;
        if (data_offset < 0xC0) data_offset += 2;
        emit_load(data_addr, 0);
    }
}

// Emit a local variable load into R0
static void emit_lvar(Node *node) {
    fprintf(outputfp, "# LVAR offset=%d\n", node->loff);

    // Compute address = FP + loff
    int temp_reg = get_temp_reg();

    // temp_reg = FP
    emit_alu(OP_ADD, temp_reg, 15, 0);   // temp = R15 (FP)

    // R0 = loff (load literal)
    int loff = node->loff;
    int data_addr = data_offset;
    if (data_offset < 0xC0) data_offset += 2;

    // Store loff into data section
    // the backend loads literals by placing them in data and LDR'ing them
    emit_load(data_addr, 0);

    // temp_reg = temp_reg + R0  (FP + loff)
    emit_alu(OP_ADD, temp_reg, temp_reg, 0);

    // Now temp_reg holds the absolute address of the local variable.
    // But LDR requires an immediate address, not a register.
    // So we spill the computed address into a temp memory slot.

    int spill_addr = data_offset;
    if (data_offset < 0xC0) data_offset += 2;

    // STR temp_reg → spill_addr
    emit_store(temp_reg, spill_addr);

    // Finally load the local variable into R0
    emit_load(spill_addr, 0);
}

// Store Rsrc into local variable at FP + loff
static void emit_store_lvar(Node *node, int src_reg) {
    fprintf(outputfp, "# STORE LVAR offset=%d from R%d\n", node->loff, src_reg);

    int loff = node->loff;

    // Step 1: temp_reg = FP (R15)
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 15, 0);   // temp = R15

    // Step 2: Load literal offset into R0
    int data_addr = data_offset;
    if (data_offset < 0xC0)
        data_offset += 2;  // allocate literal slot

    // Store loff into data section (same pattern as emit_literal)
    emit_load(data_addr, 0);  // R0 = loff

    // Step 3: temp_reg = FP + loff
    emit_alu(OP_ADD, temp_reg, temp_reg, 0);

    // Step 4: Spill computed address into a temp memory slot
    int spill_addr = data_offset;
    if (data_offset < 0xC0)
        data_offset += 2;

    emit_store(temp_reg, spill_addr);

    // Step 5: Finally store src_reg → [spill_addr]
    emit_store(src_reg, spill_addr);
}

// Emit a global variable load into R0
static void emit_gvar(Node *node) {
    fprintf(outputfp, "# GVAR %s\n", node->glabel ? node->glabel : "(unknown)");

    // Look up the global variable's address in the label_map
    int addr = get_label_addr(node->glabel);

    // If not found, allocate it now
    if (addr == 0) {
        addr = data_offset;
        if (data_offset < 0xC0)
            data_offset += 2;  // allocate 1 word
        // Register the global label with its address
        char *addr_str = malloc(16);
        snprintf(addr_str, 16, "0x%02x", addr);
        map_put(label_map, node->glabel, addr_str);
    }

    // Load the global variable into R0
    emit_load(addr, 0);
}

static void emit_store_gvar(Node *node, int src_reg) {
    int addr = get_label_addr(node->glabel);
    emit_store(src_reg, addr);
}

// Emit a function call (stub for now)
static void emit_func_call(Node *node) {
    fprintf(outputfp, "# CALL %s\n", node->fname ? node->fname : "(unknown)");

    // 1. Evaluate arguments (left-to-right)
    int argc = vec_len(node->args);
    for (int i = 0; i < argc && i < 4; i++) {
        Node *arg = vec_get(node->args, i);

        // Evaluate argument into R0
        emit_expr(arg);

        // Move R0 → R(1+i)
        emit_alu(OP_ADD, 1 + i, 0, 0);
    }

    // 2. Load return address into R15
    // Return address = next instruction (code_offset + 1)
    int retaddr = code_offset + 1;

    // Spill retaddr into data section so we can load it
    int data_addr = data_offset;
    if (data_offset < 0xC0)
        data_offset += 2;

    // Store literal retaddr into data section
    // (Your backend loads literals via LDR from data)
    emit_load(data_addr, 15);   // R15 = retaddr

    // 3. Jump to function entry
    int func_addr = get_label_addr(node->fname);
    emit_jmp(func_addr);

    // 4. After return, result is already in R0
}

// Convert between primitive types
// Convert the value in R0 from operand->ty to node->ty
static void emit_conv(Node *node) {
    // Step 1: evaluate operand → R0
    emit_expr(node->operand);

    Type *from = node->operand->ty;
    Type *to   = node->ty;

    //
    // 1. BOOL conversion: (value != 0)
    //
    if (to->kind == KIND_BOOL) {
        char *l_true = gen_ex_isa_make_label();
        char *l_end  = gen_ex_isa_make_label();

        // if R0 != 0 → jump to true
        emit_jnz(get_label_addr(l_true), 0);

        // false: R0 = 0
        emit_alu(OP_SUB, 0, 0, 0);
        emit_jmp(get_label_addr(l_end));

        // true: R0 = 1
        register_label(l_true);
        
        emit_literal_into_R0(1);

        register_label(l_end);
        return;
    }


    if (to->kind == KIND_INT || to->kind == KIND_LONG || to->kind == KIND_LLONG) {
        // No-op: R0 already holds a 16-bit integer
        return;
    }

    //
    // 3. Converting to unsigned char (8-bit)
    //
    if (to->kind == KIND_CHAR && to->usig) {
        // mask = 0x00FF
        int mask_reg = get_temp_reg();
        emit_literal_into_reg(mask_reg, 0x00FF);
        emit_alu(OP_AND, 0, 0, mask_reg);
        return;
    }

    //
    // 4. Converting to signed char (8-bit)
    //
    if (to->kind == KIND_CHAR && !to->usig) {
        // sign bit = 0x80
        int sign_reg = get_temp_reg();
        emit_literal_into_reg(sign_reg, 0x0080);

        char *l_neg = gen_ex_isa_make_label();
        char *l_end = gen_ex_isa_make_label();

        // if (R0 & 0x80) != 0 → negative
        emit_alu(OP_AND, 0, 0, sign_reg);
        emit_jnz(get_label_addr(l_neg), 0);

        // positive: mask to 8 bits
        emit_literal_into_reg(sign_reg, 0x00FF);
        emit_alu(OP_AND, 0, 0, sign_reg);
        emit_jmp(get_label_addr(l_end));

        // negative: OR with 0xFF00
        register_label(l_neg);
        emit_literal_into_reg(sign_reg, 0xFF00);
        emit_alu(OP_OR, 0, 0, sign_reg);

        register_label(l_end);
        return;
    }

    //
    // 5. Converting to unsigned short (16-bit)
    //
    if (to->kind == KIND_SHORT && to->usig) {
        // No-op: ISA is 16-bit
        return;
    }

    //
    // 6. Converting to signed short (16-bit)
    //
    if (to->kind == KIND_SHORT && !to->usig) {
        // No-op: ISA is 16-bit and already sign-preserving
        return;
    }

    //
    // 7. Default: no conversion needed
    //
    return;
}

static void emit_load_convert(Type *to, Type *from) {
    //since we only have shorts, any special types won't actually do anything. woohoo!
    if (to->kind == KIND_BOOL)
        emit_to_bool(from);
    else if (is_inttype(from) && is_inttype(to))
        emit_intcast(from);
    else if (is_inttype(to))
        emit_toint(from);

}

static void emit_to_bool(Type *ty) {
    if(is_flotype(ty)) {
        
    } else {

    }
}

static void emit_ioint(Type *ty) {

}

static void emit_intcast(Type *ty) {
    
}

// Emit binary arithmetic: result in R0
// Pattern: left operand -> R0, right operand -> R1, ALU op -> R0 = R0 op R1
static void emit_binop_add(Node *node) {
    emit_expr(node->left);      // result in R0
    emit_alu(OP_SUB, 1, 1, 1);  // R1 = 0 (clear R1)
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0 (save left)
    emit_expr(node->right);     // result in R0
    emit_alu(OP_ADD, 0, temp_reg, 0);  // R0 = temp + R0
}

static void emit_binop_sub(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0 (save left)
    emit_expr(node->right);     // result in R0
    emit_alu(OP_SUB, 0, temp_reg, 0);  // R0 = temp - R0
}

static void emit_binop_mul(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0 (save left)
    emit_expr(node->right);     // result in R0
    emit_alu(OP_MULT, 0, temp_reg, 0); // R0 = temp * R0
}

static void emit_binop_xor(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0
    emit_expr(node->right);     // result in R0

    //replace XOR with the hacked-in version
    emit_alu(OP_XOR, 0, temp_reg, 0);  // R0 = temp ^ R0
}

static void emit_binop_or(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0
    emit_expr(node->right);     // result in R0
    emit_alu(OP_OR, 0, temp_reg, 0);   // R0 = temp | R0
}

static void emit_binop_and(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0
    emit_expr(node->right);     // result in R0
    emit_alu(OP_AND, 0, temp_reg, 0);  // R0 = temp & R0
}

static void emit_binop_shl(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0
    emit_expr(node->right);     // shift amount in R0
    emit_alu(OP_SHL, 0, temp_reg, 0);  // R0 = temp << R0 (simplified)
}

static void emit_binop_shr(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0
    emit_expr(node->right);     // shift amount in R0
    emit_alu(OP_SHR, 0, temp_reg, 0);  // R0 = temp >> R0 (simplified)
}

// Comparison: set R0 to 1 if true, 0 if false
// TODO: implement JLT-based comparisons
static void emit_binop_lt(Node *node) {
    emit_expr(node->left);      // result in R0
    int temp_reg = get_temp_reg();
    emit_alu(OP_ADD, temp_reg, 0, 0);  // temp = R0
    emit_expr(node->right);     // result in R0
    // If temp < R0: R0 = 1, else R0 = 0
    // Use JLT and conditional jumps
    emit_jlt_instr(code_offset + 4, temp_reg, 0);  // Jump if temp < R0
    emit_alu(OP_SUB, 0, 0, 0);  // R0 = 0 (not true)
    emit_jmp(code_offset + 2);  // Jump over next instruction
    // If we jumped here: R0 = 1
    fprintf(outputfp, "# TODO: set R0 = 1 on jump target\n");
}

// Emit a conditional jump (less-than): JLT addr, r1, r2
// If r1 < r2, jump to addr
static void emit_jlt_instr(int addr, int r1, int r2) {
    // JLT format: [OP_JLT(4)][RA(4)][RB(4)][ADDR(4)]
    // For now, simplified: just emit a NOP as placeholder
    fprintf(outputfp, "# JLT 0x%02x, R%d, R%d\n", addr, r1, r2);
    emit_instr_hex(make_instr(OP_JLT, r1, r2, addr & 0xF));
}

// Emit an expression node recursively
static void emit_expr(Node *node) {
    if (!node) return;
    
    switch (node->kind) {
    case AST_LITERAL:
        emit_literal(node);
        break;
    case AST_LVAR:
        emit_lvar(node);
        break;
    case AST_GVAR:
        emit_gvar(node);
        break;
    case AST_FUNCALL:
    case AST_FUNCPTR_CALL:
        emit_func_call(node);
        break;
    case '+':
        emit_binop_add(node);
        break;
    case '-':
        emit_binop_sub(node);
        break;
    case '*':
        emit_binop_mul(node);
        break;
    case '/':
    case '%':
        fprintf(outputfp, "# TODO: division/modulo not yet implemented\n");
        emit_alu(OP_SUB, 0, 0, 0);
        break;
    case '^':
        emit_binop_xor(node);
        break;
    case '|':
        emit_binop_or(node);
        break;
    case '&':
        emit_binop_and(node);
        break;
    case OP_SAL:
    case OP_SHL:
        emit_binop_shl(node);
        break;
    case OP_SAR:
    case OP_SHR:
        emit_binop_shr(node);
        break;
    case '<':
        emit_binop_lt(node);
        break;
    case '>':
    case OP_LE:
    case OP_GE:
    case OP_EQ:
    case OP_NE:
        fprintf(outputfp, "# TODO: comparison operators not yet fully implemented\n");
        emit_alu(OP_SUB, 0, 0, 0);
        break;
    case AST_RETURN:
        if (node->retval) {
            emit_expr(node->retval);
            // Result is now in R0
        } else {
            // Return 0
            emit_alu(OP_SUB, 0, 0, 0);
        }
        // Emit return instruction (jump to return address in R15)
        
        emit_jmp(0); // TODO: use actual return address
        break;
    case AST_IF: {
        // Evaluate condition
        fprintf(outputfp, "# IF statement\n");
        emit_expr(node->cond);  // result in R0
        
char *else_label = gen_ex_isa_make_label();
    char *end_label = gen_ex_isa_make_label();
        
        // If R0 == 0 (condition false), jump to else_label
        emit_jnz(get_label_addr(else_label), 0);
        
        // Then branch
        if (node->then) emit_expr(node->then);
        emit_jmp(get_label_addr(end_label));
        
        // Else branch
        register_label(else_label);
        if (node->els) emit_expr(node->els);
        
        // End
        register_label(end_label);
        break;
    }
    case AST_COMPOUND_STMT:
        // For each statement in the compound block
        for (int i = 0; i < vec_len(node->stmts); i++) {
            emit_expr((Node *)vec_get(node->stmts, i));
        }
        break;
    case AST_FUNC:
        // Function body
        if (node->body)
            emit_expr(node->body);
        break;
    default:
        // Other node types to be implemented
        fprintf(outputfp, "# TODO: unimplemented node kind %d\n", node->kind);
        break;
    }
}

// ============================================================================
// Function prologue and epilogue, with stack frame helper method
// ============================================================================

static void emit_func_prologue(Node *func, int frame_size) {
    fprintf(outputfp, "# Function prologue: %s\n",
            func->fname ? func->fname : "(unnamed)");

    int locals = frame_size;

    // --- 1. Push old FP ---
    stack_ptr--;
    emit_store(14, stack_ptr);

    // --- 2. Push return address ---
    stack_ptr--;
    emit_store(15, stack_ptr);

    // --- 3. New FP = current SP ---
    emit_load(stack_ptr, 14);

    // --- 4. Allocate locals ---
    if (locals > 0) {
        stack_ptr -= locals;
    }

    // --- 5. Save callee-saved registers R8–R14 ---
    for (int r = 8; r <= 14; r++) {
        stack_ptr--;
        emit_store(r, stack_ptr);
    }
}


static void emit_func_epilogue(Node *func, int frame_size) {
    fprintf(outputfp, "# Function epilogue: %s\n",
            func->fname ? func->fname : "(unnamed)");

    int locals = frame_size;

    // --- 1. Restore callee-saved registers R8–R14 ---
    for (int r = 14; r >= 8; r--) {
        emit_load(stack_ptr, r);
        stack_ptr++;
    }

    // --- 2. Deallocate locals ---
    if (locals > 0) {
        stack_ptr += locals;
    }

    // --- 3. Restore return address into R15 ---
    emit_load(stack_ptr, 15);
    stack_ptr++;

    // --- 4. Restore old FP ---
    emit_load(stack_ptr, 14);
    stack_ptr++;

    // --- 5. Spill R15 into a temp memory slot ---
    int spill_addr = data_offset;
    if (data_offset < 0xC0)
        data_offset += 2;

    emit_store(15, spill_addr);   // store R15 → [spill_addr]

    // --- 6. Load spilled return address into R0 ---
    emit_load(spill_addr, 0);     // R0 = return address

    // --- 7. Jump to R0 (absolute) ---
    emit_jmp(0);                  // JMP R0_value (patched below)
}

static void push_if_stmt(Vector *stack, Node *n) {
    if (!n) return;

    switch (n->kind) {

    case AST_COMPOUND_STMT:
        for (int i = 0; i < vec_len(n->stmts); i++)
            vec_push(stack, vec_get(n->stmts, i));
        break;

    case AST_IF:
        vec_push(stack, n->cond);
        vec_push(stack, n->then);
        vec_push(stack, n->els);
        break;

    case AST_RETURN:
        vec_push(stack, n->retval);
        break;

    case AST_TERNARY:
        vec_push(stack, n->cond);
        vec_push(stack, n->then);
        vec_push(stack, n->els);
        break;

    case AST_FUNCALL:
    case AST_FUNCPTR_CALL:
        for (int i = 0; i < vec_len(n->args); i++)
            vec_push(stack, vec_get(n->args, i));
        if (n->kind == AST_FUNCPTR_CALL)
            vec_push(stack, n->fptr);
        break;

    case AST_DEREF:
    case AST_ADDR:
    case AST_STRUCT_REF:
        vec_push(stack, n->operand);
        break;

    case AST_LVAR:
        break;

    default:
        break;
    }
}

// Compute max positive frame size from all local variables in this function.
// Assumes loff is a non-negative offset from FP (your R14/R15 choice).
static int ex_isa_compute_frame_size(Node *func) {
    int maxoff = 0;

    Vector *stack = make_vector();
    vec_push(stack, func->body);

    while (vec_len(stack) > 0) {
        Node *n = vec_pop(stack);
        if (!n) continue;

        if (n->kind == AST_LVAR) {
            if (n->loff > maxoff)
                maxoff = n->loff;
            continue;
        }

        push_if_stmt(stack, n);
    }

    if (maxoff % 2)
        maxoff++;

    return maxoff;
}

// ============================================================================
// Top-level code generation entry point
// ============================================================================

void gen_ex_isa_emit_toplevel(Node *v) {
    if (!v) return;
    
    switch (v->kind) {
    case AST_FUNC: {
        // Emit function label and prologue
            //debug!
            printf("got to function label generation for:%s\n", v->fname);

        fprintf(outputfp, "# --- Function: %s (addr 0x%02x) ---\n",
                v->fname ? v->fname : "(unnamed)", code_offset);
        
            printf("got to frame size calculation for:%s\n", v->fname);

        int frame_size = ex_isa_compute_frame_size(v);

            printf("got to function prologue generation for:%s\n", v->fname);

        emit_func_prologue(v, frame_size);

        // Emit function body
        if (v->body) {
            emit_expr(v->body);
        } else {
            // Empty function: return 0
            emit_alu(OP_SUB, 0, 0, 0);
        }
        
        emit_func_epilogue(v, frame_size);

        printf("got past function epilogue generation for:%s\n", v->fname);
        
        fprintf(outputfp, "# --- End function ---\n");
        break;
    }
    case AST_DECL: {
        // Global variable declaration
        fprintf(outputfp, "# Global: %s (addr 0x%02x)\n",
                v->declvar ? v->declvar->glabel : "(unnamed)", data_offset);
        
        // Allocate space in data section
        if (data_offset < 0xC0) {
            data_offset += 2;  // Each global takes at least 2 bytes (1 word)
        }
        
        break;
    }
    default:
        // Other top-level forms not yet implemented
        break;
    }
}
