
#include <stdlib.h>
#include <stdio.h>
void exit(int);

void die(void *msg) {
    fprintf(stderr, "DIE: %s\n", (const char*)msg);
    exit(1);
}
