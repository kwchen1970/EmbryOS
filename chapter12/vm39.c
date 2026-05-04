#include "embryos.h"

enum { V, R, W, X, U, G, A, D };
#define PTE(x)              ((uword_t) 1 << (x))
#define PT_ENTRY(pa, flags) ((((uword_t)(pa) & ~ (uword_t) 0xFFF) >> 2) | (flags))
#define PTE_TO_PA(pte)      (((pte) & ~(uword_t)0x3FF) << 2)
#define PTE_COUNT           (PAGE_SIZE / sizeof(uword_t))
#define BPW                 (8 * sizeof(uword_t))   // bits per word
#define RW                  (PTE(V)|PTE(R)|PTE(W)|PTE(A)|PTE(D))
#define RWX                 (RW|PTE(X))

#if VBITS == 39

void vm_map(void *base, uintptr_t va, void *frame) {
    uword_t *l1 = base;
    int vpn1 = (va >> 21) & (PTE_COUNT - 1);
    int vpn0 = (va >> 12) & (PTE_COUNT - 1);
    uword_t pte1 = l1[vpn1];
    uword_t *l0;
    if (!(pte1 & PTE(V))) {
        l0 = frame_alloc();
        memset(l0, 0, PAGE_SIZE);
        l1[vpn1] = PT_ENTRY((uintptr_t)l0, PTE(V));
    } else {
        l0 = (uword_t *)PTE_TO_PA(pte1);
    }
    l0[vpn0] = PT_ENTRY((uintptr_t)frame, RWX|PTE(U));
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

int vm_is_mapped(void *base, uintptr_t va) {
    uword_t *l1 = base;
    int vpn1 = (va >> 21) & (PTE_COUNT - 1);
    int vpn0 = (va >> 12) & (PTE_COUNT - 1);
    uword_t pte1 = l1[vpn1];
    if (!(pte1 & PTE(V))) return 0;
    uword_t *l0 = (uword_t *)PTE_TO_PA(pte1);
    return l0[vpn0] & PTE(V);
}

void vm_flush(struct hart *hart, void *base) {
    uword_t *l1 = base;
    int start = (VM_START >> 21) & (PTE_COUNT - 1);
    int count = (VM_END - VM_START) >> 21;
    for (int i = 0; i < count; i++)
        hart->parent_page_table[start + i] = l1[start + i];
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

void vm_release(void *base) {
    uword_t *l1 = base;
    for (int i = 0; i < PTE_COUNT; i++) {
        uword_t pte1 = l1[i];
        if (pte1 & PTE(V)) {
            uword_t *l0 = (uword_t *)PTE_TO_PA(pte1);
            for (int j = 0; j < PTE_COUNT; j++) {
                uword_t pte0 = l0[j];
                if (pte0 & PTE(V)) frame_release((void *)PTE_TO_PA(pte0));
            }
            frame_release(l0);
        }
    }
}

void vm_init(struct hart *hart) {
    kprintf("vm_init start hart %d\n", hart->idx);
    uword_t *root_pt = frame_alloc();
    hart->parent_page_table = frame_alloc();
    memset(hart->parent_page_table, 0, PAGE_SIZE);
    for (int i = 0; i < PTE_COUNT; i++)
        root_pt[i] = PT_ENTRY(i * (0x1ULL << 30), RWX|PTE(G));
    root_pt[VM_START >> 30] =
        PT_ENTRY((uword_t) hart->parent_page_table, PTE(V));
    uword_t base = (VM_START >> 30) << 30;
    for (int i = 0; i < PTE_COUNT; i++)
        hart->parent_page_table[i] = PT_ENTRY(base + i * (0x1ULL << 21), RWX|PTE(G));
    int vm_start_idx = (VM_START >> 21) & (PTE_COUNT - 1);
    int vm_count = (VM_END - VM_START) >> 21;
    for (int i = 0; i < vm_count; i++)
        hart->parent_page_table[vm_start_idx + i] = 0;
    if (hart->idx == 0) kprintf("Enabling virtual memory now\n");
    vm_enable(((uword_t) 1 << (BPW - 1)) | (((uword_t) root_pt) >> 12));
    asm volatile("sfence.vma zero, zero" ::: "memory");
    if (hart->idx == 0) kprintf("Virtual memory enabled\n");
    struct pcb *self = sched_self();
    if (self != 0 && self->base != 0)
        vm_flush(hart, self->base);
}

#endif // VBITS == 39
