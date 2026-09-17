// Code-generation test for the EX_ISA backend.
// Exercises calls, conditional branches, loops, arithmetic, and pointers.

static int sum_to(int limit) {
    int sum = 0;
    int value = 1;

    while (value <= limit) {
        sum += value;
        value++;
    }
    return sum;
}

static int update_value(int *value) {
    *value = *value * 2 + 1;
    return *value;
}

int main(void) {
    int value = sum_to(5);

    if (update_value(&value) != 31)
        return 1;
    if (value != 31)
        return 2;
    return 0;
}