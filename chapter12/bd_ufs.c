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

}

static void ufs_write_inode(struct ufs_state *s, int inode, const struct ufs_inode *in) {

}

int ufs_alloc(void *st){
    for (){

    }
    return 0;
}

int ufs_size(void *st, int inode){

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