#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    volatile int lock;
    volatile int int_status;
}spinlock_t;

void spinlock_init(spinlock_t *s);
void spinlock_lock(spinlock_t *s);
int  spinlock_try_lock(spinlock_t *s);
void spinlock_unlock(spinlock_t *s);
void spinlock_lock_interrupt(spinlock_t *s);
void spinlock_unlock_interrupt(spinlock_t *s);
int  spinlock_try_lock_interrupt(spinlock_t *s);
#endif