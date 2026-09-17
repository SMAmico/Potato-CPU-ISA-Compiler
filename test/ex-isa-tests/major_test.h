#ifndef MAJOR_TEST_H
#define MAJOR_TEST_H

#include <stddef.h>
//#include "testmain.c"

size_t strlen(const char *);

extern void print(char *s);
extern void ffail(char *file, int line, char *msg);
extern void fexpect(char *file, int line, int expected, int actual);
extern void fexpectf(char *file, int line, float expected, float actual);
extern void fexpectd(char *file, int line, double expected, double actual);
extern void fexpectl(char *file, int line, long expected, long actual);

#define fail(msg) ffail(__FILE__, __LINE__, msg)
#define expect(expected, actual) fexpect(__FILE__, __LINE__, expected, actual)
#define expectf(expected, actual) fexpectf(__FILE__, __LINE__, expected, actual)
#define expectd(expected, actual) fexpectd(__FILE__, __LINE__, expected, actual)
#define expectl(expected, actual) fexpectl(__FILE__, __LINE__, expected, actual)

#endif
