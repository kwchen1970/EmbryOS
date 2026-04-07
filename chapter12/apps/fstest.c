#include "stdio.h"
#include "syslib.h"
#include "bd.h"

static int failures;

static void check(int cond, const char *msg) {
    if (cond) printf("PASS: %s\n", msg);
    else {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

void main(void) {
    int f1 = user_create();
    check(f1 > 0, "create empty file");
    check(user_size(f1) == 0, "empty file starts at size 0");

    char ch = 'X';
    check(user_read(f1, 0, &ch, 1) == 0, "empty file read returns EOF");
    check(ch == 'X', "empty file read does not overwrite buffer");

    int f2 = user_create();
    int off = 3 * BLOCK_SIZE + 17;
    char z = 'Z';
    char out = 1;

    check(user_write(f2, off, &z, 1) == 1, "sparse write succeeds");
    check(user_size(f2) == off + 1, "sparse write updates size");

    out = 1;
    check(user_read(f2, 0, &out, 1) == 1 && out == 0, "hole at file start reads as zero");
    out = 1;
    check(user_read(f2, BLOCK_SIZE + 9, &out, 1) == 1 && out == 0,
          "hole in indirect region reads as zero");
    out = 0;
    check(user_read(f2, off, &out, 1) == 1 && out == 'Z', "written byte reads back");

    z = 'Q';
    check(user_write(f2, off, &z, 1) == 1, "overwrite sparse byte succeeds");
    out = 0;
    check(user_read(f2, off, &out, 1) == 1 && out == 'Q', "overwrite reads back");

    user_delete(f2);
    int f3 = user_create();
    check(f3 > 0, "create after delete succeeds");

    z = 'A';
    check(user_write(f3, BLOCK_SIZE, &z, 1) == 1, "write after delete succeeds");
    out = 0;
    check(user_read(f3, BLOCK_SIZE, &out, 1) == 1 && out == 'A',
          "read after delete succeeds");

    user_delete(f1);
    user_delete(f3);

    if (failures == 0) printf("fstest: all checks passed\n");
    else printf("fstest: %d checks failed\n", failures);
}
