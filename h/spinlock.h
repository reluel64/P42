#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

typedef struct spinlock_t
{
    volatile int lock;
}spinlock_t;

typedef struct spinlock_rw_t
{
    volatile uint32_t lock;
}spinlock_rw_t;

void spinlock_init(spinlock_t *s);
void spinlock_lock(spinlock_t *s);
void spinlock_unlock(spinlock_t *s);
void spinlock_unlock_int(spinlock_t *s, int state);
void spinlock_lock_int(spinlock_t *s, int *state);
#endif