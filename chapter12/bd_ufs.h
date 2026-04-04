#pragma once
#include "bd.h"
#include "stdint.h"

#define UFS_PTRS_PER_BLOCK (BLOCK_SIZE/sizeof(uint32_t))
#define UFS_INODES_PER_BLOCK (BLOCK_SIZE/(4*sizeof(uint32_t)))
#define UFS_MAX_FILE_BLOCKS (1 + UFS_PTRS_PER_BLOCK + UFS_PTRS_PER_BLOCK* UFS_PTRS_PER_BLOCK)


struct ufs_superblock {
    uint32_t inode_blocks;
    uint32_t free_list_head;
};

struct ufs_inode {
    uint32_t allocated;
    uint32_t direct;
    uint32_t indirect;
    uint32_t double_indirect;
};

struct ufs_ptr_block {
    uint32_t ptrs[UFS_PTRS_PER_BLOCK];
};

struct ufs_state {
 struct bd*lower;
 int inode_below;
 int n_inodes;
 struct ufs_superblock sb;
};

int ufs_alloc(void *st);
int ufs_size(void *st, int inode);
void ufs_read(void *st, int inode, int blk, void *dst);
void ufs_write(void *st, int inode, int blk, const void *src);
void ufs_free(void *st, int inode);

void ufs_init(struct bd*iface, struct ufs_state *s, struct bd *lower, int inode_below, int n_inodes);