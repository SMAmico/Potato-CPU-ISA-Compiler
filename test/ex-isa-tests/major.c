#include <stdbool.h>
#include <stddef.h>
#include "major_test.h"

#define SQUARE(x) ((x) * (x))

typedef unsigned long word;

enum status {
    STATUS_IDLE = 1,
    STATUS_READY,
    STATUS_DONE
};

struct point {
    int x;
    int y;
};

union value {
    int number;
    unsigned char bytes[sizeof(int)];
};

static int global_counter;
static const int constant_value = 42;

static int add(int left, int right) {
    return left + right;
}

static int factorial(int value) {
    if (value <= 1)
        return 1;
    return value * factorial(value - 1);
}

static int sum_array(const int *values, size_t length) {
    int result = 0;
    size_t i;
    for (i = 0; i < length; i++)
        result += values[i];
    return result;
}

static int point_sum(struct point point) {
    return point.x + point.y;
}

static void test_scalars_and_expressions(void) {
    int value = 7;
    unsigned int bits = 0x5a;
    bool truth = false;

    expect(12, 5 + 7);
    expect(-7, -value);
    expect(8, ++value);
    expect(8, value++);
    expect(9, value);
    expect(1, 3 < 4 && 4 <= 4);
    expect(1, 0 || 2);
    expect(1, !truth);
    expect(0x0a, bits & 0x0f);
    expect(0x5f, bits | 0x05);
    expect(0x50, bits ^ 0x0a);
    expect(0xb4, bits << 1);
    expect(0x2d, bits >> 1);
    expect(16, SQUARE(4));
    expect(10, (value > 8) ? 10 : 20);
    expect(3, (value = 3, value));
}

static void test_types_and_conversions(void) {
    char character = 'A';
    short small = 12;
    long large = 1000L;
    long long very_large = 2000LL;
    float single = 1.5f;
    double twice = 2.5;
    word unsigned_word = 9;

    expect(65, character);
    expect(12, small);
    expectl(1000L, large);
    expectl(2000L, very_large);
    expectf(3.0, single * 2);
    expectd(5.0, twice * 2);
    expect(9, (int)unsigned_word);
    expect(2, (int)twice);
    expect(sizeof(char), 1);
    expect(sizeof(int) >= sizeof(short), 1);
    expect(sizeof(long) >= sizeof(int), 1);
}

static void test_control_flow(void) {
    int total = 0;
    int i;

    for (i = 0; i < 10; i++) {
        if (i == 2)
            continue;
        if (i == 7)
            break;
        total += i;
    }
    expect(19, total);

    i = 0;
    while (i < 3)
        i++;
    expect(3, i);

    do {
        i--;
    } while (i > 0);
    expect(0, i);

    switch (STATUS_READY) {
    case STATUS_IDLE:
        total = 0;
        break;
    case STATUS_READY:
        total = 42;
        break;
    default:
        total = -1;
    }
    expect(42, total);

    i = 0;
    goto assigned;
    i = 100;
assigned:
    expect(0, i);
}

static void test_functions_and_storage(void) {
    int (*operation)(int, int) = add;
    static int static_value;

    expect(9, operation(4, 5));
    expect(120, factorial(5));
    expect(42, constant_value);

    global_counter++;
    static_value++;
    expect(1, global_counter);
    expect(1, static_value);
}

static void test_arrays_pointers_and_strings(void) {
    int values[4] = { 2, 4, 6, 8 };
    int *cursor = values;
    char text[] = "hello";
    const char *literal = "world";

    expect(20, sum_array(values, 4));
    expect(2, *cursor);
    expect(8, *(cursor + 3));
    expect(6, cursor[2]);
    expect('h', text[0]);
    expect('o', text[4]);
    expect('\0', text[5]);
    expect('w', literal[0]);
    expect('d', *(literal + 4));
    expect(6, sizeof(text));
    expect(5, strlen(text));
}

static void test_aggregates(void) {
    struct point first = { 3, 4 };
    struct point points[2] = { { 1, 2 }, { 5, 6 } };
    struct point *point = &first;
    union value value;

    expect(7, point_sum(first));
    expect(3, first.x);
    expect(4, point->y);
    point->x = 10;
    expect(14, first.x + first.y);
    expect(2, points[0].y);
    expect(11, points[1].x + points[1].y);

    value.number = 1234;
    expect(1234, value.number);
    expect(1, sizeof(value) >= sizeof(value.number));
}

static void test_qualifiers_and_short_circuit(void) {
    volatile int observed = 0;
    int result = 0;

    observed = 5;
    if (observed == 5 && (result = 9))
        expect(9, result);
    if (0 && (observed = 100))
        fail("short-circuit evaluation");
    expect(5, observed);
}

void testmain(void) {
    print("major language features");
    test_scalars_and_expressions();
    test_types_and_conversions();
    test_control_flow();
    test_functions_and_storage();
    test_arrays_pointers_and_strings();
    test_aggregates();
    test_qualifiers_and_short_circuit();
}