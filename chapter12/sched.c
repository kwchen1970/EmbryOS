#include "embryos.h"

#define N_PRIORITIES 3

struct pcb *run_queue[N_PRIORITIES];
struct pcb *sleep_queue = 0;        // queue for sleeping processes

extern uint64_t ticks_to_ns(uint64_t ticks);

void sched_resume(struct pcb *pcb) { proc_enqueue(&run_queue[0], pcb); }

void sched_block(struct pcb *old) {
    int p = 0;      // first find highest priority process
    while (p < N_PRIORITIES && run_queue[p] == 0) p++;
    struct pcb *new = proc_dequeue(&run_queue[p]);
    if (new != old) {       // switch needed
        new->hart = old->hart;
        new->hart->needs_tlb_flush = 1;
        sched_set_self(new);
        L3(L_FREQ, L_CTX_SWITCH, (uintptr_t) old, (uintptr_t) new, new->hart->id);
        ctx_switch(&old->sp, new->sp);
        reap_zombies();
    }
}

void sched_run(int executable, struct rect area, void *args, int size) {
    struct pcb *old = sched_self();
    proc_enqueue(&run_queue[0], old);
    struct pcb *new = proc_create(old->hart, executable, area, args, size);
    sched_set_self(new);
    L4(L_NORM, L_CTX_START, (uintptr_t) old, (uintptr_t) new, new->hart->id, executable);
    ctx_start(&old->sp, (char *) new + PAGE_SIZE);
    reap_zombies();
}

void sched_yield(void) {
    struct pcb *current = sched_self();
    if (current->executable > 0) {     // idle loop doesn't yield at interrupts
        proc_enqueue(&run_queue[1], current);
        sched_block(current);
    }
}

void sched_sleep(uint64_t deadline) {
    struct pcb *current = sched_self();
    uint64_t current_time = ticks_to_ns(mtime_get());
    
    // If deadline is in the past or now, return immediately
    if (deadline <= current_time) {
        return;
    }
    
    // Store the deadline and move to sleep queue
    current->sleep_deadline = deadline;
    L2(L_NORM, L_USER_SLEEP_START, (uintptr_t) current, deadline);
    
    if (sleep_queue == 0) {
        current->next = current;        // single element circular list
        sleep_queue = current;
    } else {
        current->next = sleep_queue->next;  // new head
        sleep_queue->next = current;        // old tail points to new
    }


    // Context switch to another process
    sched_block(current);
}

void sched_wake_sleeping(void) {
    if (sleep_queue == 0) return;
    uint64_t now = ticks_to_ns(mtime_get());

    // count nodes once so can examine each sleeper at most once
    int n = 0;
    struct pcb *p = sleep_queue->next;
    do {
        n++;
        p = p->next;
    } while (p != sleep_queue->next);
    struct pcb *prev = sleep_queue; // starts with tail 
    struct pcb *curr = sleep_queue->next; //the head

    for (int i = 0; i <n && sleep_queue != 0; i++) {
        struct pcb *next = curr->next;
        if (curr->sleep_deadline <=now){
            //need to unlink the curr
            if (curr==prev){
                sleep_queue = 0; // only one element
            } else {
                prev->next = next; // unlink curr
                if (curr == sleep_queue) {
                    sleep_queue = prev; // removes tail
                }
            }
            curr->sleep_deadline = 0; // clear deadline
            sched_resume(curr); // move to run queue
            curr = next; // prev stays same after removal 
        } else {
            prev = curr;
            curr = next;
        }
    }
    
    
}
