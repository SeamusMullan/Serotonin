#include <stdio.h>
#include <stdlib.h>

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Hello from newlib!\n");
    char *p = malloc(42);
    printf("malloc gave me %p\n", p);
    return 0;
}
