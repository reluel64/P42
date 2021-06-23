#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

typedef struct spinlock_t
{
    volatile int lock;
    volatile uint32_t int_lock_cnt;
    volatile int pre_lock_state;
}spinlock_t;


void spinlock_init(spinlock_t *s);
void spinlock_lock(spinlock_t *s);
void spinlock_unlock(spinlock_t *s);
void spinlock_unlock_int(spinlock_t *s);
void spinlock_lock_int(spinlock_t *s);
void spinlock_write_unlock_int(spinlock_t *s);
void spinlock_write_lock_int(spinlock_t *s);
void spinlock_read_unlock_int(spinlock_t *s);
void spinlock_read_lock_int(spinlock_t *s);
void spinlock_rw_init(spinlock_t *s);
#endif