#pragma once

#include "bd.h"
#include "embryos.h"

// The "RAM disk" is an in-memory implementation of a disk
// abstraction.
struct ramdisk_state {
    int nblocks;    // # blocks of the disk
    char *data;     // where in memory the data is stored
    int next_inode; // next inode number to allocate
    int max_inodes; // maximum number of inodes supported
    int *free_inodes; // dynamically allocated free list for inodes
    int free_inodes_count;
};

// Initialize a RAM disk.  iface is a pointer to the generic block
// disk interface, state points to where state of the ram disk is
// kept, mem is where the storage is, and nblocks the size.
void ramdisk_init(struct bd *iface, struct ramdisk_state *state,
                    void *mem, int nblocks);
