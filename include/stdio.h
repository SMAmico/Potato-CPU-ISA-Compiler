// Copyright 2012 Rui Ueyama. Released under the MIT license.

#ifndef __STDIO_H
#define __STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct __8cc_FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsprintf(char *buffer, const char *format, va_list args);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
int fflush(FILE *stream);
int fileno(FILE *stream);

#endif