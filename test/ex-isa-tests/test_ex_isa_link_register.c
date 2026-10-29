static int factorial(int value) {
    if (value)
        return factorial(value - 1) + 1;
    return 0;
}

int main(void) {
    int (*operation)(int) = factorial;
    return operation(2) - 2;
}