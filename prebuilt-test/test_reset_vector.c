int add3(int a, int b, int c) {
    return a + b + c;
}

int twice(int x) {
    return add3(x, x, 0);
}

int main() {
    int r = twice(5);
    return r;
}
