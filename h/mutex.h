#ifndef mutexh
#define mutexh
#include <linked_list.h>
#include <spinlock.h>
#include <sched.h>

#define MUTEX_RECUSRIVE (1 << 0)
#define MUTEX_FIFO      (1 << 1)
#define MUTEX_PRIO      (1 << 2)
#define MUTEX_TASK_ORDER (MUTEX_FIFO | MUTEX_PRIO)
#define MUTEX_LOWEST_PRIO  SCHED_MAX_PRIORITY

struct mutex
{
    struct list_head pendq;
    struct spinlock lock;
    volatile void *owner;
    volatile uint32_t rlevel;
    volatile uint32_t owner_prio;
    int opts;
};

struct mutex *mtx_init
(
    struct mutex *mtx, 
    int options
);

struct mutex *mtx_create
(
    int options
);

int mtx_acquire
(
    struct mutex *mtx, 
    uint32_t wait_ms
);

int mtx_release
(
    struct mutex *mtx
);
#endif