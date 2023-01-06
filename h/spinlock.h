#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

#define SPINLOCK_INIT {.lock = 0};
typedef struct spinlock_t
{
    volatile int lock;
}spinlock_t;



void spinlock_init
(
    spinlock_t *s
);

void spinlock_lock
(
    spinlock_t *s
);

void spinlock_unlock
(
    spinlock_t *s
);

void spinlock_unlock_int
(
    spinlock_t *s, 
    uint8_t flag
);

void spinlock_lock_int
(
    spinlock_t *s, 
    uint8_t *flag
);

void spinlock_write_unlock_int
(
    spinlock_t *s,
    uint8_t flag
);

void spinlock_write_lock_int
(
    spinlock_t *s, 
    uint8_t *flag
);

void spinlock_read_unlock_int
(
    spinlock_t *s, 
    uint8_t flag
);

void spinlock_read_lock_int
(
    spinlock_t *s, 
    uint8_t *flag
);

void spinlock_rw_init
(
    spinlock_t *s
);

void spinlock_write_unlock
(
    spinlock_t *s
);

void spinlock_write_lock
(
    spinlock_t *s
);

void spinlock_read_unlock
(
    spinlock_t *s
);

void spinlock_read_lock
(
    spinlock_t *s
);

#endif