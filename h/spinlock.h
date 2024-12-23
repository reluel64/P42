#ifndef spinlock_h
#define spinlock_h

#include <stddef.h>
#include <stdint.h>

#define SPINLOCK_INIT {.lock = 0};
#define SPINLOCK_RW_INIT {.lock = UINT32_MAX};

typedef struct spinlock_t
{
    volatile uint32_t lock;
}spinlock_t;

typedef struct spinlock_rw_t
{
    volatile uint32_t lock;
}spinlock_rw_t;



void spinlock_init
(
    spinlock_t *s
);

void spinlock_lock
(
    spinlock_t *s
);

int8_t spinlock_trylock
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

int8_t spinlock_trylock_int
(
    spinlock_t *s, 
    uint8_t *flag
)
;
void spinlock_write_unlock_int
(
    spinlock_rw_t *s,
    uint8_t flag
);

void spinlock_write_lock_int
(
    spinlock_rw_t *s, 
    uint8_t *flag
);

void spinlock_read_unlock_int
(
    spinlock_rw_t *s, 
    uint8_t flag
);

void spinlock_read_lock_int
(
    spinlock_rw_t *s, 
    uint8_t *flag
);

void spinlock_rw_init
(
    spinlock_rw_t *s
);

void spinlock_write_unlock
(
    spinlock_rw_t *s
);

void spinlock_write_lock
(
    spinlock_rw_t *s
);

void spinlock_read_unlock
(
    spinlock_rw_t *s
);

void spinlock_read_lock
(
    spinlock_rw_t *s
);

#endif