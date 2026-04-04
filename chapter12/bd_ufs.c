#include "embryos.h"

static int ufs_inode_blocknode(int inode){
    return 1 + inode/UFS_INODES_PER_BLOCK;
}

static int ufs_inode_offset(int inode){
    return inode % UFS_INODES_PER_BLOCK;
}

static void ufs_write_super(struct ufs_state *s){
    struct block *b = bd_alloc();
    memset(b, 0,size-f(*b));
    memcpy(b, &s->sb, sizeof(s->sb));
    bd_free(b);
}

static void ufs_read_inode(struct ufs_state *s, int inode, struct ufs_inode *out){
    struct block *b = bd_alloc();
    struct ufs_inode *table;

    if (inode <=0 || inode > s->n_inodes) die("ufs_read_inode: bad inode");
    s->lower->read(s->lower->state, s->inode_below, ufs_inode_blockno(inode),b);
    table = (struct ufs_inode *) b;
    *out = table[ufs_inode_offset(inode)];
    bd_free(b);
}

static void ufs_write_inode(struct ufs_state *s, int inode, const struct ufs_inode *in) {
    struct block *b = bd_alloc();
    struct ufs_inode *table;

    if (inode <= 0 || inode > s->n_inodes) die("ufs_write_inode: bad inode");

    s->lower->read(s->lower->state, s->inode_below, ufs_inode_blockno(inode), b);
    table = (struct ufs_inode *) b;
    table[ufs_inode_offset(inode)] = *in;
    s->lower->write(s->lower->state, s->inode_below, ufs_inode_blockno(inode), b);


    bd_free(b);
}

int ufs_alloc(void *st){
    struct ufs_state *s = st;
    struct ufs_inode in;

    for (int inode = 1; inode <=s->n_inodes; inode++){
        ufs_read_inode(s,inode,&in);
        if (!in.allocated) {
            memset(&in, 0, sizeof(in));
            in.allocated =1;
            ufs_write_inode(s,inode,&in);
            return inode;
        }

    }
    return 0;
}

int ufs_size(void *st, int inode){
    (void) st;
    (void) inode;
    return UFS_MAX_FILE_BLOCKS;

}

void ufs_read (void *st, int inode, int blk, void *dst) {

}
void ufs_write(void *st, int inode, int blk, const void *src){

}
void ufs_free(void *st, int inode){

}

void ufs_init(struct bd *iface, struct ufs_state *s,
              struct bd *lower, int inode_below, int n_inodes){

}