#include "thread.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);

// Define these external functions from syscall interface
extern int user_get(int block);
extern uint64_t user_gettime(void);
extern void user_put(int col, int row, uint16_t cell);
extern void user_exit();

// Import context switching primitives
extern void ctx_switch(void **old_sp, void *new_sp);
extern void ctx_start(void **save_sp, void *new_sp);

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
static int next_tid = 0;
static int input_buffer = -1;  // -1 means empty

// Forward declarations
static void schedule(void);
static void wakeup_sleeping_threads(void);
static int try_wakeup_input(void);

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

// Try to wake up input waiter if input available
static int try_wakeup_input(void) {
    if (!waiting_input || input_buffer == -1) {
        return 0;
    }
    
    int result = input_buffer;
    input_buffer = -1;
    thread_t *waiter = waiting_input;
    waiting_input = waiter->next;
    waiter->next = NULL;
    enqueue_runnable(waiter);
    return result;
}

// Pick next runnable thread
static thread_t *next_runnable(void) {
    wakeup_sleeping_threads();
    try_wakeup_input();
    
    return dequeue_runnable();
}

// Main scheduler - chooses next thread to run
static void schedule(void) {
    thread_t *next = next_runnable();
    
    // If no threads are runnable, try to get input to wake waiters
    while (!next && waiting_input) {
        int c = user_get(1);  // Blocking call
        if (c >= -2 && c <= 255) {
            input_buffer = c;
            next = next_runnable();
            if (!next && waiting_input) {
                // Still no runnable threads, but input arrived
                // Deliver it and try again
                continue;
            }
        }
        if (!next) break;
    }
    
    // If still no runnable threads, all threads are blocked on semaphores
    if (!next) {
        // Program should exit - all remaining threads are blocked
        user_put(0, 0, 0);  // Dummy operation to keep process alive briefly
        user_exit();
    }
    
    if (next == current_thread) {
        // Continue running current thread
        return;
    }
    
    thread_t *prev = current_thread;
    current_thread = next;
    
    if (prev) {
        // Switch from previous thread to next thread
        ctx_switch(&prev->sp, next->sp);
    } else {
        // This is the first time we're switching to a thread
        ctx_start(&next->sp, next->sp);
    }
}

// Called when a new thread starts (must be called from thread context)
void exec_user(void) {
    // This is called via ctx_start when a thread starts
    // The thread function and argument are already on the stack
    // This is a placeholder - actual thread functions handle their own logic
}

// Initialize the threading system
void thread_init(void) {
    // The main thread (current_thread) is already running
    // We just need to set up the initial state
    current_thread = NULL;  // Will be set when first thread is created
    runnable_queue = NULL;
    sleeping_queue = NULL;
    waiting_input = NULL;
    input_buffer = -1;
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
    
    // Set up stack with return address and argument
    // Stack grows downward; sp points to top(lowest address)
    unsigned char *sp = stack + stack_size;  // Top of stack
    
    // Align stack to 16 bytes (required for many ABIs)
    sp = (unsigned char *)((unsigned long)sp & ~15UL);
    
    // Push thread function and argument for thread wrapper
    sp -= sizeof(void *);  // Space for return address
    *(void **)sp = (void *)&thread_exit;  // Return address
    
    sp -= sizeof(void *);
    *(void **)sp = arg;
    
    sp -= sizeof(void *);
    *(void **)sp = (void *)f;
    
    t->sp = sp;
    
    // Add to runnable queue
    enqueue_runnable(t);
}

// Yield control to next thread
void thread_yield(void) {
    thread_t *prev = current_thread;
    current_thread = NULL;
    
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
    current_thread = NULL;
    
    if (prev) {
        enqueue_sleeping(prev, deadline);
    }
    
    schedule();
}

// Blocking input - returns character or focus change event
int thread_get(void) {
    // Check if input is already available
    if (input_buffer != -1) {
        int result = input_buffer;
        input_buffer = -1;
        return result;
    }
    
    // Block current thread waiting for input
    thread_t *prev = current_thread;
    current_thread = NULL;
    
    if (prev) {
        prev->state = THREAD_WAITING_INPUT;
        prev->next = waiting_input;
        waiting_input = prev;
    }
    
    schedule();
    
    // When we resume, input should be in input_buffer
    return input_buffer;
}

// Exit current thread
void thread_exit(void) {
    thread_t *prev = current_thread;
    current_thread = NULL;
    
    if (prev) {
        prev->state = THREAD_DEAD;
    }
    
    // Schedule next thread (and never return)
    schedule();
    
    // Should never reach here
    user_exit();
}

// Create a semaphore
struct sema *sema_create(unsigned int count) {
    struct sema *s = (struct sema *)malloc(sizeof(struct sema));
    if (!s) user_exit;
    
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
    current_thread = NULL;
    
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
