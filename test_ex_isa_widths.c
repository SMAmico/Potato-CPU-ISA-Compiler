// Register-width smoke test for the EX_ISA backend.

int main(void) {
    signed char narrow = -1;
    unsigned char small = 255;
    short word = 0x1234;
    int wide = 0x12345;
    return (narrow + small + word + wide) & 0xffff;
}