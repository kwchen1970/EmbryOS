#include "thread.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

// Define these external functions from syscall interface
extern int user_get(int block);
extern uint64_t user_gettime(void);
extern void user_sleep(uint64_t deadline);
extern void user_put(int row, int col, uint16_t cell);
extern void user_exit();

// Import context switching primitives
extern void ctx_switch(void **old_sp, void *new_sp);
extern void ctx_start(void **save_sp, void *new_sp);

#define INPUT_Q_CAP 64
static int input_q[INPUT_Q_CAP];
static unsigned int input_q_head = 0;
static unsigned int input_q_tail = 0;
static unsigned int input_q_count = 0;

// Thread state enum
typedef enum {
    THREAD_RUNNABLE = 0,
    THREAD_SLEEPING = 1,
    THREAD_WAITING_INPUT = 2,
    THREAD_WAITING_SEMA = 3,
    THREAD_DEAD = 4
} thread_state_t;

// Thread control block
typedef struct thread {
    // Core data
    int tid;
    void *stack_base;
    unsigned int stack_size;
    void *sp;  // Stack pointer for context switching
    
    // State management
    thread_state_t state;
    uint64_t wake_time;  // For sleeping threads
    struct sema *waiting_sema;  // For semaphore-blocked threads
    
    // Linked list pointers
    struct thread *next;

    void (*start_fn)(void *);
    void *start_arg;
    int started;

    int input_event; 

} thread_t;

// Semaphore structure
struct sema {
    unsigned int count;
    thread_t *waiting_list;  // List of threads waiting on this semaphore
};

// Global scheduler state
static thread_t *runnable_queue = NULL;
static thread_t *sleeping_queue = NULL;
static thread_t *waiting_input = NULL;
static thread_t *current_thread = NULL;
static thread_t *zombie_list = NULL;
static int next_tid = 0;

// Forward declarations
static void schedule(void);
static void wakeup_sleeping_threads(void);
static void try_wakeup_input(void);
static int input_q_push(int ev);
static int input_q_pop(int *ev);
static void reap_zombies(void);

// Add thread to runnable queue
static void enqueue_runnable(thread_t *t) {
    if (!t) return;
    t->state = THREAD_RUNNABLE;
    t->next = NULL;
    
    if (!runnable_queue) {
        runnable_queue = t;
    } else {
        thread_t *iter = runnable_queue;
        while (iter->next) iter = iter->next;
        iter->next = t;
    }
}

// Remove thread from runnable queue (only remove first occurrence)
static thread_t *dequeue_runnable(void) {
    thread_t *t = runnable_queue;
    if (t) {
        runnable_queue = t->next;
        t->next = NULL;
    }
    return t;
}

// Add thread to sleeping queue (keeping sorted by wake time)
static void enqueue_sleeping(thread_t *t, uint64_t wake_time) {
    t->state = THREAD_SLEEPING;
    t->wake_time = wake_time;
    t->next = NULL;
    
    if (!sleeping_queue) {
        sleeping_queue = t;
    } else if (wake_time < sleeping_queue->wake_time) {
        t->next = sleeping_queue;
        sleeping_queue = t;
    } else {
        thread_t *iter = sleeping_queue;
        while (iter->next && iter->next->wake_time <= wake_time) {
            iter = iter->next;
        }
        t->next = iter->next;
        iter->next = t;
    }
}

// Check if thread is in sleeping queue and remove it
static void remove_from_sleeping(thread_t *t) {
    if (!sleeping_queue) return;
    
    if (sleeping_queue == t) {
        sleeping_queue = t->next;
        return;
    }
    
    thread_t *iter = sleeping_queue;
    while (iter && iter->next) {
        if (iter->next == t) {
            iter->next = t->next;
            return;
        }
        iter = iter->next;
    }
}

// Wake up all sleeping threads whose deadline has passed
static void wakeup_sleeping_threads(void) {
    uint64_t now = user_gettime();
    
    while (sleeping_queue && sleeping_queue->wake_time <= now) {
        thread_t *t = sleeping_queue;
        sleeping_queue = t->next;
        enqueue_runnable(t);
    }
}

// Deliver one pending input event to one waiting thread.
static void try_wakeup_input(void) {
    int ev;
    while (waiting_input && input_q_pop(&ev)) {
        thread_t *w = waiting_input;
        waiting_input = w->next;
        w->next = NULL;
        w->input_event = ev;
        enqueue_runnable(w);
    }
}

static int input_q_push(int ev) {
    if (input_q_count >= INPUT_Q_CAP) return 0;
    input_q[input_q_tail] = ev;
    input_q_tail = (input_q_tail + 1) % INPUT_Q_CAP;
    input_q_count++;
    return 1;
}

static int input_q_pop(int *ev) {
    if (input_q_count == 0) return 0;
    *ev = input_q[input_q_head];
    input_q_head = (input_q_head + 1) % INPUT_Q_CAP;
    input_q_count--;
    return 1;
}

static void reap_zombies(void) {
    while (zombie_list) {
        thread_t *z = zombie_list;
        zombie_list = z->next;
        if (z->stack_base) free(z->stack_base);
        free(z);
    }
}

// Pick next runnable thread
static thread_t *next_runnable(void) {
    wakeup_sleeping_threads();
    try_wakeup_input();
    
    return dequeue_runnable();
}

// Main scheduler - chooses next thread to run
static void schedule(void) {
    while (1) {
        // Don't reap when called from thread_exit() before switching away:
        // in that case current_thread is NULL and we are still on the exiting stack.
        if (current_thread) {
            reap_zombies();
        }

        thread_t *next = next_runnable();

        if (next) {
            if (next == current_thread) return;

            thread_t *prev = current_thread;
            current_thread = next;

            if (!prev) {
                static void *dummy_sp;
                if (!next->started) {
                    next->started = 1;
                    ctx_start(&dummy_sp, next->sp);
                } else {
                    ctx_switch(&dummy_sp, next->sp);
                }
            } else if (!next->started) {
                next->started = 1;
                ctx_start(&prev->sp, next->sp);
            } else {
                ctx_switch(&prev->sp, next->sp);
            }
            return;
        }

        // No runnable thread: if input waiters exist, block for one input event.
        if (waiting_input) {
            int ev = user_get(1);
            // If queue is full, deliver directly to one waiting thread.
            if (!input_q_push(ev) && waiting_input) {
                thread_t *w = waiting_input;
                waiting_input = w->next;
                w->next = NULL;
                w->input_event = ev;
                enqueue_runnable(w);
            }
            try_wakeup_input();
            continue;
        }

        // No runnable and no input waiters: sleep until nearest wake deadline.
        if (sleeping_queue) {
            user_sleep(sleeping_queue->wake_time);
            continue;
        }

        // No runnable/sleeping/input-waiting thread left => process can end.
        user_exit();
    }
}

// Called when a new thread starts (must be called from thread context)
void exec_user(void) {
    current_thread->start_fn(current_thread->start_arg);
    thread_exit();   // must not return
}


// Initialize the threading system
void thread_init(void) {
    // The main thread (current_thread) is already running
    // We just need to set up the initial state
    current_thread = NULL;  // Will be set when first thread is created
    runnable_queue = NULL;
    sleeping_queue = NULL;
    waiting_input = NULL;
    zombie_list = NULL;
    input_q_head = input_q_tail = input_q_count = 0;
    next_tid = 0;
    
    // Create and switch to main thread (becomes first thread)
    // We'll do this by creating a thread for the current execution context
    thread_t *main_thread = malloc(sizeof(thread_t));
    if (!main_thread) user_exit();
    
    main_thread->tid = next_tid++;
    main_thread->stack_base = NULL;  // Main thread uses existing stack
    main_thread->stack_size = 0;
    main_thread->sp = NULL;
    main_thread->state = THREAD_RUNNABLE;
    main_thread->wake_time = 0;
    main_thread->waiting_sema = NULL;
    main_thread->next = NULL;
    main_thread->started = 1;
    main_thread->start_fn = NULL;
    main_thread->start_arg = NULL;
    main_thread->input_event = 0;
    
    current_thread = main_thread;
}

// Create and start a new thread
void thread_create(void (*f)(void *), void *arg, unsigned int stack_size) {
    // Allocate stack - malloc aligns at 16 bytes

    unsigned char *stack = (unsigned char *)malloc(stack_size);
    if (!stack) user_exit();
    
    // Allocate thread control block
    thread_t *t = (thread_t *)malloc(sizeof(thread_t));
    if (!t) {
        free(stack);
        user_exit();
    }
    
    // Initialize thread control block
    t->tid = next_tid++;
    t->stack_base = stack;
    t->stack_size = stack_size;
    t->state = THREAD_RUNNABLE;
    t->wake_time = 0;
    t->waiting_sema = NULL;

    t->start_fn = f;
    t->start_arg = arg;
    t->started = 0;

    t->input_event = 0;



    
    // Set up stack with return address and argument
    // Stack grows downward; sp points to top(lowest address)
    unsigned char *sp = stack + stack_size;  // Top of stack
    
    // Align stack to 16 bytes (required for many ABIs)
    sp = (unsigned char *)((unsigned long)sp & ~15UL);
    
    t->sp = sp;
    
    // Add to runnable queue
    enqueue_runnable(t);
}

// Yield control to next thread
void thread_yield(void) {
    thread_t *prev = current_thread;
  
    
    // Add current thread back to runnable queue
    if (prev) {
        enqueue_runnable(prev);
    }
    
    // Schedule next thread
    schedule();
}

// Sleep until deadline (in nanoseconds)
void thread_sleep(uint64_t deadline) {
    thread_t *prev = current_thread;

    
    if (prev) {
        enqueue_sleeping(prev, deadline);
    }
    
    schedule();
}

int thread_get(void) {
    int ev;
    if (input_q_pop(&ev)) {
        return ev;
    }

    thread_t *self = current_thread;
    self->state = THREAD_WAITING_INPUT;
    self->next = waiting_input;
    waiting_input = self;

    schedule();
    return current_thread->input_event;
}


// Exit current thread
void thread_exit(void) {
    thread_t *prev = current_thread;
    current_thread = NULL;
    
    if (prev) {
        prev->state = THREAD_DEAD;
        prev->next = zombie_list;
        zombie_list = prev;
    }
    
    // Schedule next thread (and never return)
    schedule();
    
    // Should never reach here
    user_exit();
}

// Create a semaphore
struct sema *sema_create(unsigned int count) {
    struct sema *s = (struct sema *)malloc(sizeof(struct sema));
    if (!s) user_exit();
    
    s->count = count;
    s->waiting_list = NULL;
    
    return s;
}

// Increment semaphore (V operation) - wake one waiter
void sema_inc(struct sema *sema) {
    if (!sema) return;
    
    if (sema->waiting_list) {
        // Wake up one waiting thread
        thread_t *waiter = sema->waiting_list;
        sema->waiting_list = waiter->next;
        waiter->next = NULL;
        waiter->waiting_sema = NULL;
        enqueue_runnable(waiter);
    } else {
        // No waiters, increment count
        sema->count++;
    }
}

// Decrement semaphore (P operation) - block if count is 0
void sema_dec(struct sema *sema) {
    if (!sema) return;
    
    if (sema->count > 0) {
        sema->count--;
        return;
    }
    
    // Block current thread waiting on semaphore
    thread_t *prev = current_thread;

    
    if (prev) {
        prev->state = THREAD_WAITING_SEMA;
        prev->waiting_sema = sema;
        
        // Add to semaphore's waiting list
        prev->next = sema->waiting_list;
        sema->waiting_list = prev;
    }
    
    schedule();
}

// Release semaphore (free resources)
void sema_release(struct sema *sema) {
    if (!sema) return;
    
    // Wake all waiting threads
    while (sema->waiting_list) {
        thread_t *waiter = sema->waiting_list;
        sema->waiting_list = waiter->next;
        waiter->next = NULL;
        waiter->waiting_sema = NULL;
        enqueue_runnable(waiter);
    }
    
    free(sema);
}
