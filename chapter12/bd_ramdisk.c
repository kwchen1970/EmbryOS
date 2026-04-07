#include <stdlib.h>
#include "embryos.h"

void *malloc(unsigned long); // workaround for broken include

static int ramdisk_size(void *st, int inode) {
    struct ramdisk_state *d = st;
    return d->nblocks;
}

static int ramdisk_alloc(void *st) {
    struct ramdisk_state *d = st;
    // Reuse freed inodes if available
    if (d->free_inodes_count > 0) {
        return d->free_inodes[--d->free_inodes_count];
    }
    if (d->next_inode == 0) d->next_inode = 1;
    if (d->next_inode > d->max_inodes) return 0;
    return d->next_inode++;
}
static void ramdisk_free(void *st, int inode) {
    struct ramdisk_state *d = st;
    int blocks_per_inode = d->nblocks / d->max_inodes;
    if (inode <= 0 || inode > d->max_inodes) return;
    char *base = d->data + (inode - 1) * blocks_per_inode * BLOCK_SIZE;
    memset(base, 0, blocks_per_inode * BLOCK_SIZE);
    if (d->free_inodes_count < d->max_inodes)
        d->free_inodes[d->free_inodes_count++] = inode;
}

static void ramdisk_read(void *st, int inode, int blk, void *dst) {
    L3(L_NORM, L_RAMDISK_READ, inode, blk, (uintptr_t) dst);
    struct ramdisk_state *d = st;
    int blocks_per_inode = d->nblocks / d->max_inodes;
    if (inode <= 0 || inode > d->max_inodes) die("ramdisk_read: bad inode");
    if ((unsigned) blk >= (unsigned)blocks_per_inode) die("ramdisk_read: bad offset");
    memset(dst, 0, BLOCK_SIZE);
    char *base = d->data + ((inode - 1) * blocks_per_inode + blk) * BLOCK_SIZE;
    memcpy(dst, base, BLOCK_SIZE);
}

static void ramdisk_write(void *st, int inode, int blk, const void *src) {
    L3(L_FREQ, L_RAMDISK_WRITE, inode, blk, (uintptr_t) src);
    struct ramdisk_state *d = st;
    int blocks_per_inode = d->nblocks / d->max_inodes;
    if (inode <= 0 || inode > d->max_inodes) die("ramdisk_write: bad inode");
    if ((unsigned) blk >= (unsigned)blocks_per_inode) die("ramdisk_write: bad offset");
    char *base = d->data + ((inode - 1) * blocks_per_inode + blk) * BLOCK_SIZE;
    memcpy(base, src, BLOCK_SIZE);
}

void ramdisk_init(struct bd *iface, struct ramdisk_state *state,
                  void *mem, int nblocks) {
    state->data = mem;
    state->nblocks = nblocks;
    state->next_inode = 1;
    state->free_inodes_count = 0;
    // Generalize: set max_inodes so each inode gets at least 8 blocks
    state->max_inodes = nblocks / 8;
    if (state->max_inodes < 1) state->max_inodes = 1;
    state->free_inodes = malloc(sizeof(int) * state->max_inodes);
    if (state->max_inodes < 1) state->max_inodes = 1;
    memset(state->data, 0, nblocks * BLOCK_SIZE);
    iface->state  = state;
    iface->alloc  = ramdisk_alloc;
    iface->size   = ramdisk_size;
    iface->read   = ramdisk_read;
    iface->write  = ramdisk_write;
    iface->free   = ramdisk_free;
}
