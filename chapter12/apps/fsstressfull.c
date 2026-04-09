#include "stdio.h"
#include "syslib.h"
#include "bd.h"
#include "bd_ufs.h"

#define CHURN_FILES 24
#define FILL_FILES 96

static int failures;

static void check(int cond, const char *msg) {
    if (cond) printf("PASS: %s\n", msg);
    else {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

static void fill_block(char *buf, char seed) {
    for (int i = 0; i < BLOCK_SIZE; i++) buf[i] = seed + (i % 23);
}

void main(void) {
    char buf[BLOCK_SIZE];
    char out;
    int saw_exhaustion = 0;

    int f = user_create();
    int dbl_off = (1 + UFS_PTRS_PER_BLOCK) * BLOCK_SIZE + 9;
    out = 0;
    check(f > 0, "create file for double-indirect test");
    check(user_write(f, dbl_off, "D", 1) == 1, "double-indirect write succeeds");
    check(user_size(f) == dbl_off + 1, "double-indirect write updates size");
    check(user_read(f, dbl_off, &out, 1) == 1 && out == 'D',
          "double-indirect byte reads back");
    out = 1;
    check(user_read(f, dbl_off - BLOCK_SIZE, &out, 1) == 1 && out == 0,
          "hole before double-indirect byte reads as zero");
    user_delete(f);

    int churn[CHURN_FILES];
    for (int i = 0; i < CHURN_FILES; i++) {
        churn[i] = user_create();
        check(churn[i] > 0, "create churn file");
        fill_block(buf, 'a' + (i % 10));
        check(user_write(churn[i], 0, buf, BLOCK_SIZE) == BLOCK_SIZE,
              "write churn block");
    }
    for (int i = 0; i < CHURN_FILES; i += 2)
        user_delete(churn[i]);
    for (int i = 0; i < CHURN_FILES; i += 2) {
        int g = user_create();
        check(g > 0, "recreate after alternating deletes");
        check(user_write(g, BLOCK_SIZE + i, "R", 1) == 1,
              "write into reused free-list state");
        user_delete(g);
    }
    for (int i = 1; i < CHURN_FILES; i += 2)
        user_delete(churn[i]);

    int created = 0;
    int files[FILL_FILES];
    for (int i = 0; i < FILL_FILES; i++) files[i] = 0;

    fill_block(buf, 'K');
    for (int i = 0; i < FILL_FILES; i++) {
        int g = user_create();
        if (g <= 0) break;
        files[i] = g;
        int ok = 1;
        for (int blk = 0; blk < 8; blk++) {
            int off = blk * BLOCK_SIZE;
            int n = user_write(g, off, buf, BLOCK_SIZE);
            int size = user_size(g);
            char check_byte = 0;
            int read_ok = user_read(g, off, &check_byte, 1);
            if (n != BLOCK_SIZE || size < off + BLOCK_SIZE ||
                read_ok != 1 || check_byte != buf[0]) {
                if (!saw_exhaustion) {
                    printf("fsstressfull: write failed at file %d block %d\n", i, blk);
                    printf("fsstressfull: disk exhaustion detected without kernel crash\n");
                    saw_exhaustion = 1;
                }
                ok = 0;
                break;
            }
        }
        if (!ok) break;
        created++;
    }
    printf("fsstressfull: created %d fill files before exhaustion\n", created);
    check(created > 0, "disk accepted at least some fill files");

    for (int i = 0; i < FILL_FILES; i++)
        if (files[i] > 0) user_delete(files[i]);

    int h = user_create();
    check(h > 0, "create after fill/delete succeeds");
    check(user_write(h, 0, "Z", 1) == 1, "write after fill/delete succeeds");
    out = 0;
    check(user_read(h, 0, &out, 1) == 1 && out == 'Z',
          "read after fill/delete succeeds");
    user_delete(h);

    if (failures == 0) printf("fsstressfull: all checks passed\n");
    else printf("fsstressfull: %d checks failed\n", failures);
}
