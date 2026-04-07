#include "stdio.h"
#include "dir.h"

void main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: rm file\n");
        return;
    }

    if (dir_lookup(argv[1]) < 0) {
        printf("%s: No such file\n", argv[1]);
        return;
    }

    dir_delete(argv[1]);
}
