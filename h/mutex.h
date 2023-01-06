#ifndef mutexh
#define mutexh
#include <linked_list.h>
#include <spinlock.h>
#include <scheduler.h>

#define MUTEX_RECUSRIVE (1 << 0)
#define MUTEX_FIFO      (1 << 1)
#define MUTEX_PRIO      (1 << 2)
#define MUTEX_TASK_ORDER (MUTEX_FIFO | MUTEX_PRIO)
#define MUTEX_LOWEST_PRIO  SCHED_MAX_PRIORITY

typedef struct mutex_t
{
    list_head_t pendq;
    spinlock_t lock;
    volatile void *owner;
    volatile uint32_t rlevel;
    volatile uint32_t owner_prio;
    int opts;

}mutex_t;

mutex_t *mtx_init
(
    mutex_t *mtx, 
    int options
);

mutex_t *mtx_create
(
    int options
);

int mtx_acquire
(
    mutex_t *mtx, 
    uint32_t wait_ms
);

int mtx_release
(
    mutex_t *mtx
);
#endif