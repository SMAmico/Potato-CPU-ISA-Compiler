typedef unsigned long size_t;

size_t strlen(const char *text) {
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

void print(char *text) {
    (void)text;
}

void ffail(char *file, int line, char *message) {
    (void)file;
    (void)line;
    (void)message;
}

void fexpect(char *file, int line, int expected, int actual) {
    (void)file;
    (void)line;
    (void)expected;
    (void)actual;
}

void fexpectf(char *file, int line, float expected, float actual) {
    (void)file;
    (void)line;
    (void)expected;
    (void)actual;
}

void fexpectd(char *file, int line, double expected, double actual) {
    (void)file;
    (void)line;
    (void)expected;
    (void)actual;
}

void fexpectl(char *file, int line, long expected, long actual) {
    (void)file;
    (void)line;
    (void)expected;
    (void)actual;
}
