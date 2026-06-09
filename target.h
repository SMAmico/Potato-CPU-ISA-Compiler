// Copyright 2026. Released under the MIT license.
// Target abstraction layer for code generation backends.
//
// This header defines a generic interface for multiple compiler backends.
// Each backend (x86-64, EX_ISA, etc.) implements these functions.

#pragma once

#include <stdio.h>
#include "8cc.h"

// Initialize the target backend
// Called once at the start of code generation
// fp: output file for assembly text
void target_init(FILE *fp);

// Finalize the target backend
// Called after all code generation is complete
// May emit final data sections, directives, etc.
void target_finalize(void);

// Emit code for a top-level AST node (function or global variable)
// Called for each top-level declaration/definition
void target_emit_toplevel(Node *v);

// Set the output file (if needed to change it after init)
void target_set_output_file(FILE *fp);
