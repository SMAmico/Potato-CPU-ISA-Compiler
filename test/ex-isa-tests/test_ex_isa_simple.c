// Simple test program for EX_ISA backend
// Tests: literals, returns, basic function

int return_42(void) {
    return 42;
}

int return_zero(void) {
    return 0;
}

int main(void) {
    return return_42();
}
