#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

#define SPINLOCK_INIT {.lock = 0};
#define SPINLOCK_RW_INIT {.lock = UINT32_MAX};

struct spinlock
{
    volatile uint32_t lock;
};

struct spinlock_rw
{
    volatile uint32_t lock;
};



void spinlock_init
(
    struct spinlock *s
);

void spinlock_lock
(
    struct spinlock *s
);

int8_t spinlock_try_lock
(
    struct spinlock *s
);

void spinlock_unlock
(
    struct spinlock *s
);

void spinlock_unlock_int
(
    struct spinlock *s, 
    uint8_t flag
);

void spinlock_lock_int
(
    struct spinlock *s, 
    uint8_t *flag
);

int8_t spinlock_try_lock_int
(
    struct spinlock *s, 
    uint8_t *flag
)
;
void spinlock_write_unlock_int
(
    struct spinlock_rw *s,
    uint8_t flag
);

void spinlock_write_lock_int
(
    struct spinlock_rw *s, 
    uint8_t *flag
);

void spinlock_read_unlock_int
(
    struct spinlock_rw *s, 
    uint8_t flag
);

void spinlock_read_lock_int
(
    struct spinlock_rw *s, 
    uint8_t *flag
);

void spinlock_rw_init
(
    struct spinlock_rw *s
);

void spinlock_write_unlock
(
    struct spinlock_rw *s
);

void spinlock_write_lock
(
    struct spinlock_rw *s
);

void spinlock_read_unlock
(
    struct spinlock_rw *s
);

void spinlock_read_lock
(
    struct spinlock_rw *s
);

#endif