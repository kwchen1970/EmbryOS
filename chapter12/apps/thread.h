#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>
#include <stdlib.h>

// Thread management
void thread_init(void);
void thread_create(void (*f)(void *), void *arg, unsigned int stack_size);
void thread_yield(void);
void thread_sleep(uint64_t deadline);
int  thread_get(void);
void thread_exit(void);

struct sema;
struct sema *sema_create(unsigned int count);
void sema_inc(struct sema *sema);
void sema_dec(struct sema *sema);
void sema_release(struct sema *sema);

#endif // THREAD_H
