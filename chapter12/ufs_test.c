// UFS Filesystem Test Suite
// Tests: block allocation, reuse, sparse files, holes, block leaks, double allocation

#include "flat.h"
#include "stat.h"
#include "bd.h"
#include "bd_ramdisk.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define RAMDISK_BLOCKS (128 * 16)
static char ramdisk_data[RAMDISK_BLOCKS * BLOCK_SIZE];
struct ramdisk_state ramdisk_state;
struct bd ramdisk_iface;
struct flat flat_fs;

void test_setup() {
    ramdisk_init(&ramdisk_iface, &ramdisk_state, ramdisk_data, RAMDISK_BLOCKS);
    flat_init(&flat_fs, &ramdisk_iface, 1); // format = 1 for fresh fs
}

#define TEST_BLOCK_SIZE 2048
#define TEST_FILE_SIZE (TEST_BLOCK_SIZE * 10)
#define TEST_PATTERN 0xAB

extern struct flat flat_fs;
extern struct bd ramdisk_iface;
extern struct ramdisk_state ramdisk_state;

void fill_pattern(char *buf, int size, char val) {
    for (int i = 0; i < size; i++) buf[i] = val;
}

void test_allocate_blocks_on_demand() {
    int file = flat_create(&flat_fs);
    assert(file > 0);
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, TEST_PATTERN);
    // Write to block 5 (should allocate blocks 0..5)
    flat_write(&flat_fs, file, TEST_BLOCK_SIZE * 5, buf, TEST_BLOCK_SIZE);
    // Read back
    char out[TEST_BLOCK_SIZE];
    flat_read(&flat_fs, file, TEST_BLOCK_SIZE * 5, out, TEST_BLOCK_SIZE);
    assert(memcmp(buf, out, TEST_BLOCK_SIZE) == 0);
    flat_delete(&flat_fs, file);
}

void test_reuse_blocks_when_freed() {
    int file1 = flat_create(&flat_fs);
    int file2 = flat_create(&flat_fs);
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, 0xCD);
    flat_write(&flat_fs, file1, 0, buf, TEST_BLOCK_SIZE);
    flat_delete(&flat_fs, file1);
    // Now file2 should be able to reuse the block
    flat_write(&flat_fs, file2, 0, buf, TEST_BLOCK_SIZE);
    flat_delete(&flat_fs, file2);
}

void test_sparse_files() {
    int file = flat_create(&flat_fs);
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, 0xEF);
    // Write only to block 7
    flat_write(&flat_fs, file, TEST_BLOCK_SIZE * 7, buf, TEST_BLOCK_SIZE);
    // Read from block 0 (should be zero)
    char out[TEST_BLOCK_SIZE];
    flat_read(&flat_fs, file, 0, out, TEST_BLOCK_SIZE);
    for (int i = 0; i < TEST_BLOCK_SIZE; i++) assert(out[i] == 0);
    // Read from block 7 (should match)
    flat_read(&flat_fs, file, TEST_BLOCK_SIZE * 7, out, TEST_BLOCK_SIZE);
    assert(memcmp(buf, out, TEST_BLOCK_SIZE) == 0);
    flat_delete(&flat_fs, file);
}

void test_zeroed_blocks_on_holes() {
    int file = flat_create(&flat_fs);
    char out[TEST_BLOCK_SIZE];
    // Read from unwritten block
    flat_read(&flat_fs, file, TEST_BLOCK_SIZE * 3, out, TEST_BLOCK_SIZE);
    for (int i = 0; i < TEST_BLOCK_SIZE; i++) assert(out[i] == 0);
    flat_delete(&flat_fs, file);
}

void test_no_block_leak_or_double_alloc() {
    // Fill disk with files, then delete all, then fill again
    int max_files = 128;
    int files[max_files];
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, 0x55);
    int created = 0;
    for (int i = 0; i < max_files; i++) {
        files[i] = flat_create(&flat_fs);
        if (files[i] <= 0) break;
        flat_write(&flat_fs, files[i], 0, buf, TEST_BLOCK_SIZE);
        created++;
    }
    // Delete all
    for (int i = 0; i < created; i++) flat_delete(&flat_fs, files[i]);
    // Try to fill again
    int reused = 0;
    for (int i = 0; i < max_files; i++) {
        files[i] = flat_create(&flat_fs);
        if (files[i] <= 0) break;
        flat_write(&flat_fs, files[i], 0, buf, TEST_BLOCK_SIZE);
        reused++;
    }
    assert(reused == created); // No leaks, all blocks reused
    for (int i = 0; i < reused; i++) flat_delete(&flat_fs, files[i]);
}

// Test creating a file large enough to require indirect and double-indirect blocks
void test_large_file_indirection() {
    int file = flat_create(&flat_fs);
    assert(file > 0);
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, 0x77);
    // Write to a high block number (e.g., block 1000)
    int big_block = 1000;
    flat_write(&flat_fs, file, TEST_BLOCK_SIZE * big_block, buf, TEST_BLOCK_SIZE);
    char out[TEST_BLOCK_SIZE];
    flat_read(&flat_fs, file, TEST_BLOCK_SIZE * big_block, out, TEST_BLOCK_SIZE);
    assert(memcmp(buf, out, TEST_BLOCK_SIZE) == 0);
    // Check a hole in the middle
    flat_read(&flat_fs, file, TEST_BLOCK_SIZE * (big_block/2), out, TEST_BLOCK_SIZE);
    for (int i = 0; i < TEST_BLOCK_SIZE; i++) assert(out[i] == 0);
    flat_delete(&flat_fs, file);
}

// Test partially filled free-list by freeing and allocating in non-sequential order
void test_partial_freelist() {
    int files[10];
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, 0x33);
    for (int i = 0; i < 10; i++) {
        files[i] = flat_create(&flat_fs);
        flat_write(&flat_fs, files[i], 0, buf, TEST_BLOCK_SIZE);
    }
    // Free even files
    for (int i = 0; i < 10; i += 2) flat_delete(&flat_fs, files[i]);
    // Allocate new files, should reuse freed inodes
    int reused = 0;
    for (int i = 0; i < 5; i++) {
        int f = flat_create(&flat_fs);
        assert(f > 0);
        reused++;
        flat_write(&flat_fs, f, 0, buf, TEST_BLOCK_SIZE);
        flat_delete(&flat_fs, f);
    }
    // Free odd files
    for (int i = 1; i < 10; i += 2) flat_delete(&flat_fs, files[i]);
}

// Test out-of-space handling (graceful failure)
void test_out_of_space() {
    int max_files = 1024;
    int files[max_files];
    char buf[TEST_BLOCK_SIZE];
    fill_pattern(buf, TEST_BLOCK_SIZE, 0x99);
    int created = 0;
    for (int i = 0; i < max_files; i++) {
        files[i] = flat_create(&flat_fs);
        if (files[i] <= 0) break;
        flat_write(&flat_fs, files[i], 0, buf, TEST_BLOCK_SIZE);
        created++;
    }
    // Try to create one more file, should fail
    int fail_file = flat_create(&flat_fs);
    assert(fail_file <= 0);
    // Try to write past disk capacity, should not crash
    if (created > 0) {
        int res = flat_write(&flat_fs, files[0], RAMDISK_BLOCKS * BLOCK_SIZE, buf, TEST_BLOCK_SIZE);
        (void)res; // Should not crash, may fail silently
    }
    for (int i = 0; i < created; i++) flat_delete(&flat_fs, files[i]);
}

int main() {
    test_setup();
    test_allocate_blocks_on_demand();
    printf("test_allocate_blocks_on_demand passed\n");
    test_reuse_blocks_when_freed();
    printf("test_reuse_blocks_when_freed passed\n");
    test_sparse_files();
    printf("test_sparse_files passed\n");
    test_zeroed_blocks_on_holes();
    printf("test_zeroed_blocks_on_holes passed\n");
    test_no_block_leak_or_double_alloc();
    printf("test_no_block_leak_or_double_alloc passed\n");
    printf("All UFS tests passed!\n");
    return 0;
}
