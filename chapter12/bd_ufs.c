#include "embryos.h"
#include "bd_ufs.h"

/* Returns the block number (within inode_below) that stores inode's record */
static int ufs_inode_blockno(int inode) {
    /*The - 1 is because inode numbers are 1-based, but array indices are 0-based*/
    return 1 + (inode - 1) / UFS_INODES_PER_BLOCK;
}

/* Returns the index within that block */
static int ufs_inode_offset(int inode) {
    return (inode - 1) % UFS_INODES_PER_BLOCK;
}

/* Write the in-memory superblock to disk (block 0 of inode_below) */
static void ufs_write_super(struct ufs_state *s) {
    struct block *b = bd_alloc();
    memset(b, 0, sizeof(*b));
    memcpy(b, &s->sb, sizeof(s->sb));
    s->lower->write(s->lower->state, s->inode_below, 0, b);
    bd_free(b);
}

static void ufs_read_inode(struct ufs_state *s, int inode, struct ufs_inode *out) {
    struct block *b = bd_alloc();
    if (inode <= 0 || inode > s->n_inodes) die("ufs_read_inode: bad inode");
    s->lower->read(s->lower->state, s->inode_below, ufs_inode_blockno(inode), b);
    struct ufs_inode *table = (struct ufs_inode *) b;
    *out = table[ufs_inode_offset(inode)];
    bd_free(b);
}

static void ufs_write_inode(struct ufs_state *s, int inode, const struct ufs_inode *in) {
    struct block *b = bd_alloc();
    if (inode <= 0 || inode > s->n_inodes) die("ufs_write_inode: bad inode");
    s->lower->read(s->lower->state, s->inode_below, ufs_inode_blockno(inode), b);
    struct ufs_inode *table = (struct ufs_inode *) b;
    table[ufs_inode_offset(inode)] = *in;
    s->lower->write(s->lower->state, s->inode_below, ufs_inode_blockno(inode), b);
    bd_free(b);
}


static int ufs_alloc_block(struct ufs_state *s) {
    if (s->sb.free_list_head == 0) return 0;

    struct ufs_ptr_block *fl = (struct ufs_ptr_block *) bd_alloc();
    s->lower->read(s->lower->state, s->inode_below, s->sb.free_list_head, fl);

    /* Scan for a non-null data entry */
    for (int i = UFS_PTRS_PER_BLOCK - 1; i >= 1; i--) {
        if (fl->ptrs[i] != 0) {
            int b = fl->ptrs[i];
            fl->ptrs[i] = 0;
            s->lower->write(s->lower->state, s->inode_below,
                            s->sb.free_list_head, fl);
            bd_free((struct block *) fl);
            return b;
        }
    }

    /* Free-list block has no data entries; return the head block itself */
    int b = s->sb.free_list_head;
    s->sb.free_list_head = fl->ptrs[0];
    ufs_write_super(s);
    bd_free((struct block *) fl);
    return b;
}

static void ufs_free_block(struct ufs_state *s, int b) {
    struct ufs_ptr_block *fl = (struct ufs_ptr_block *) bd_alloc();

    if (s->sb.free_list_head == 0) {
        /* Free list empty; make b the new free-list head */
        memset(fl, 0, sizeof(*fl));
        s->lower->write(s->lower->state, s->inode_below, b, fl);
        s->sb.free_list_head = b;
        ufs_write_super(s);
        bd_free((struct block *) fl);
        return;
    }

    /* Read current head */
    s->lower->read(s->lower->state, s->inode_below, s->sb.free_list_head, fl);

    /* Try to add b into an empty slot */
    for (int i = 1; i < (int) UFS_PTRS_PER_BLOCK; i++) {
        if (fl->ptrs[i] == 0) {
            fl->ptrs[i] = b;
            s->lower->write(s->lower->state, s->inode_below,
                            s->sb.free_list_head, fl);
            bd_free((struct block *) fl);
            return;
        }
    }

    /* Current head is full; make b a new free-list head pointing to old head */
    memset(fl, 0, sizeof(*fl));
    fl->ptrs[0] = s->sb.free_list_head;
    s->lower->write(s->lower->state, s->inode_below, b, fl);
    s->sb.free_list_head = b;
    ufs_write_super(s);
    bd_free((struct block *) fl);
}

int ufs_alloc(void *st) {
    struct ufs_state *s = st;
    struct ufs_inode in;

    for (int inode = 1; inode <= s->n_inodes; inode++) {
        ufs_read_inode(s, inode, &in);
        if (!in.allocated) {
            memset(&in, 0, sizeof(in));
            in.allocated = 1;
            ufs_write_inode(s, inode, &in);
            return inode;
        }
    }
    return 0;
}

int ufs_size(void *st, int inode) {
    (void) st;
    (void) inode;
    return UFS_MAX_FILE_BLOCKS;
}

void ufs_read(void *st, int inode, int blk, void *dst) {
    struct ufs_state *s = st;
    struct ufs_inode in;

    if (blk < 0 || blk >= UFS_MAX_FILE_BLOCKS) die("ufs_read: bad block");
    ufs_read_inode(s, inode, &in);

    int data_blk = 0;

    if (blk == 0) {
        data_blk = in.direct;
    } else if (blk <= (int) UFS_PTRS_PER_BLOCK) {
        /* Indirect: blk 1..UFS_PTRS_PER_BLOCK map to ptrs[0..N-1] */
        if (in.indirect == 0) { memset(dst, 0, BLOCK_SIZE); return; }
        struct ufs_ptr_block *pb = (struct ufs_ptr_block *) bd_alloc();
        s->lower->read(s->lower->state, s->inode_below, in.indirect, pb);
        data_blk = pb->ptrs[blk - 1];
        bd_free((struct block *) pb);
    } else {
        /* Double-indirect: blk UFS_PTRS_PER_BLOCK+1 .. max */
        int di = blk - (int) UFS_PTRS_PER_BLOCK - 1;
        int ind_idx = di / (int) UFS_PTRS_PER_BLOCK;
        int ptr_idx = di % (int) UFS_PTRS_PER_BLOCK;

        if (in.double_indirect == 0) { memset(dst, 0, BLOCK_SIZE); return; }
        struct ufs_ptr_block *pb = (struct ufs_ptr_block *) bd_alloc();
        s->lower->read(s->lower->state, s->inode_below, in.double_indirect, pb);
        int ind_blk = pb->ptrs[ind_idx];
        bd_free((struct block *) pb);

        if (ind_blk == 0) { memset(dst, 0, BLOCK_SIZE); return; }
        pb = (struct ufs_ptr_block *) bd_alloc();
        s->lower->read(s->lower->state, s->inode_below, ind_blk, pb);
        data_blk = pb->ptrs[ptr_idx];
        bd_free((struct block *) pb);
    }

    if (data_blk == 0)
        memset(dst, 0, BLOCK_SIZE);
    else
        s->lower->read(s->lower->state, s->inode_below, data_blk, dst);
}

void ufs_write(void *st, int inode, int blk, const void *src) {
    struct ufs_state *s = st;
    struct ufs_inode in;

    if (blk < 0 || blk >= UFS_MAX_FILE_BLOCKS) die("ufs_write: bad block");
    ufs_read_inode(s, inode, &in);
    int inode_dirty = 0;

    if (blk == 0) {
        if (in.direct == 0) {
            int newblk = ufs_alloc_block(s);
            if (newblk == 0) return;
            in.direct = newblk;
            inode_dirty = 1;
        }
        s->lower->write(s->lower->state, s->inode_below, in.direct, src);

    } else if (blk <= (int) UFS_PTRS_PER_BLOCK) {
        struct ufs_ptr_block *pb = (struct ufs_ptr_block *) bd_alloc();
        int indirect_blk = in.indirect;
        int new_indirect = 0;
        int data_blk;

        if (indirect_blk == 0) {
            new_indirect = ufs_alloc_block(s);
            if (new_indirect == 0) {
                bd_free((struct block *) pb);
                return;
            }
            indirect_blk = new_indirect;
            memset(pb, 0, sizeof(*pb));
        } else {
            s->lower->read(s->lower->state, s->inode_below, indirect_blk, pb);
        }

        data_blk = pb->ptrs[blk - 1];
        if (data_blk == 0) {
            data_blk = ufs_alloc_block(s);
            if (data_blk == 0) {
                if (new_indirect != 0) ufs_free_block(s, new_indirect);
                bd_free((struct block *) pb);
                return;
            }
            pb->ptrs[blk - 1] = data_blk;
        }

        if (new_indirect != 0) {
            in.indirect = indirect_blk;
            inode_dirty = 1;
        }
        if (new_indirect != 0 || pb->ptrs[blk - 1] == data_blk)
            s->lower->write(s->lower->state, s->inode_below, indirect_blk, pb);
        s->lower->write(s->lower->state, s->inode_below, data_blk, src);
        bd_free((struct block *) pb);

    } else {
        int di = blk - (int) UFS_PTRS_PER_BLOCK - 1;
        int ind_idx = di / (int) UFS_PTRS_PER_BLOCK;
        int ptr_idx = di % (int) UFS_PTRS_PER_BLOCK;
        struct ufs_ptr_block *pb = (struct ufs_ptr_block *) bd_alloc();
        struct ufs_ptr_block *ipb = (struct ufs_ptr_block *) bd_alloc();
        int dbl_blk = in.double_indirect;
        int ind_blk;
        int data_blk;
        int new_double = 0;
        int new_indirect = 0;

        if (dbl_blk == 0) {
            new_double = ufs_alloc_block(s);
            if (new_double == 0) {
                bd_free((struct block *) ipb);
                bd_free((struct block *) pb);
                return;
            }
            dbl_blk = new_double;
            memset(pb, 0, sizeof(*pb));
        } else {
            s->lower->read(s->lower->state, s->inode_below, dbl_blk, pb);
        }

        ind_blk = pb->ptrs[ind_idx];
        if (ind_blk == 0) {
            new_indirect = ufs_alloc_block(s);
            if (new_indirect == 0) {
                if (new_double != 0) ufs_free_block(s, new_double);
                bd_free((struct block *) ipb);
                bd_free((struct block *) pb);
                return;
            }
            ind_blk = new_indirect;
            memset(ipb, 0, sizeof(*ipb));
        } else {
            s->lower->read(s->lower->state, s->inode_below, ind_blk, ipb);
        }

        data_blk = ipb->ptrs[ptr_idx];
        if (data_blk == 0) {
            data_blk = ufs_alloc_block(s);
            if (data_blk == 0) {
                if (new_indirect != 0) ufs_free_block(s, new_indirect);
                if (new_double != 0) ufs_free_block(s, new_double);
                bd_free((struct block *) ipb);
                bd_free((struct block *) pb);
                return;
            }
            ipb->ptrs[ptr_idx] = data_blk;
        }

        if (new_double != 0) {
            in.double_indirect = dbl_blk;
            inode_dirty = 1;
        }
        if (new_indirect != 0) {
            pb->ptrs[ind_idx] = ind_blk;
            s->lower->write(s->lower->state, s->inode_below, dbl_blk, pb);
        } else if (new_double != 0) {
            s->lower->write(s->lower->state, s->inode_below, dbl_blk, pb);
        }
        if (new_indirect != 0 || ipb->ptrs[ptr_idx] == data_blk)
            s->lower->write(s->lower->state, s->inode_below, ind_blk, ipb);
        s->lower->write(s->lower->state, s->inode_below,
                        data_blk, src);
        bd_free((struct block *) ipb);
        bd_free((struct block *) pb);
    }

    if (inode_dirty) ufs_write_inode(s, inode, &in);
}

void ufs_free(void *st, int inode) {
    struct ufs_state *s = st;
    struct ufs_inode in;

    ufs_read_inode(s, inode, &in);
    if (!in.allocated) die("ufs_free: inode not allocated");

    if (in.direct != 0)
        ufs_free_block(s, in.direct);

    /* Free indirect block's data blocks, then the indirect block itself */
    if (in.indirect != 0) {
        struct ufs_ptr_block *pb = (struct ufs_ptr_block *) bd_alloc();
        s->lower->read(s->lower->state, s->inode_below, in.indirect, pb);
        for (int i = 0; i < (int) UFS_PTRS_PER_BLOCK; i++)
            if (pb->ptrs[i] != 0) ufs_free_block(s, pb->ptrs[i]);
        bd_free((struct block *) pb);
        ufs_free_block(s, in.indirect);
    }

    /* Free double-indirect: for each indirect block, free its data blocks */
    if (in.double_indirect != 0) {
        struct ufs_ptr_block *dpb = (struct ufs_ptr_block *) bd_alloc();
        s->lower->read(s->lower->state, s->inode_below, in.double_indirect, dpb);
        for (int i = 0; i < (int) UFS_PTRS_PER_BLOCK; i++) {
            if (dpb->ptrs[i] != 0) {
                struct ufs_ptr_block *ipb = (struct ufs_ptr_block *) bd_alloc();
                s->lower->read(s->lower->state, s->inode_below,
                               dpb->ptrs[i], ipb);
                for (int j = 0; j < (int) UFS_PTRS_PER_BLOCK; j++)
                    if (ipb->ptrs[j] != 0) ufs_free_block(s, ipb->ptrs[j]);
                bd_free((struct block *) ipb);
                ufs_free_block(s, dpb->ptrs[i]);
            }
        }
        bd_free((struct block *) dpb);
        ufs_free_block(s, in.double_indirect);
    }

    memset(&in, 0, sizeof(in));
    ufs_write_inode(s, inode, &in);
}

void ufs_init(struct bd *iface, struct ufs_state *s,
              struct bd *lower, int inode_below, int n_inodes) {
    s->lower      = lower;
    s->inode_below = inode_below;

    int total_blocks   = lower->size(lower->state, inode_below);
    int n_inode_blocks = (n_inodes + UFS_INODES_PER_BLOCK - 1) / UFS_INODES_PER_BLOCK;
    s->n_inodes        = n_inode_blocks * UFS_INODES_PER_BLOCK;
    s->sb.inode_blocks = n_inode_blocks;
    s->sb.free_list_head = 0;

    /* Zero-initialize all inode blocks */
    struct block *b = bd_alloc();
    memset(b, 0, sizeof(*b));
    for (int i = 1; i <= n_inode_blocks; i++)
        lower->write(lower->state, inode_below, i, b);
    bd_free(b);

    /* Write the initial superblock */
    ufs_write_super(s);

    /* Push all data blocks onto the free list */
    int first_data = 1 + n_inode_blocks;
    for (int i = first_data; i < total_blocks; i++)
        ufs_free_block(s, i);

    iface->state = s;
    iface->alloc = ufs_alloc;
    iface->size  = ufs_size;
    iface->read  = ufs_read;
    iface->write = ufs_write;
    iface->free  = ufs_free;
}
