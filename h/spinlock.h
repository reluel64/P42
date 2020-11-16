#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

typedef struct spinlock_t
{
    volatile int lock;
    volatile int int_status;
}spinlock_t;

void spinlock_init(spinlock_t *s);
void spinlock_lock(spinlock_t *s);
void spinlock_unlock(spinlock_t *s);
void spinlock_unlock_interrupt(spinlock_t *s, int state);
void spinlock_lock_interrupt(spinlock_t *s, int *state);
#endif