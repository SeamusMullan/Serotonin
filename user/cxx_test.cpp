extern "C" int write(int, const void*, unsigned);

struct Test {
    Test() {
        write(1, "bob the builder\n", 16);
    }
    ~Test() {
        write(1, "destructor ran\n", 14);
    }
};

Test t;

int main() {
    write(1, "HELLO CPP\n", 10);

    int* x = new int(123);
    if (*x == 123)
        write(1, "yo\n", 3);
    delete x;

    return 0;
}
