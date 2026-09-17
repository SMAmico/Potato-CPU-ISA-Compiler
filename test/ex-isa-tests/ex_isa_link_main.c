extern int external_value;
int external_function(void);

int main(void) {
    return external_function() + external_value - 49;
}