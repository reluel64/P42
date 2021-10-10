#ifndef mutexh
#define mutexh
#include <linked_list.h>
#include <spinlock.h>

#define MUTEX_RECUSRIVE (1 << 0)

typedef struct mutex_t
{
    list_head_t pendq;
    spinlock_t lock;
    volatile void *owner;
    volatile uint32_t rlevel;
    int opts;
}mutex_t;

mutex_t *mtx_init(mutex_t *mtx, int options);
mutex_t *mtx_create(int options);
int mtx_acquire(mutex_t *mtx, uint32_t wait_ms);
int mtx_release(mutex_t *mtx);
#endif