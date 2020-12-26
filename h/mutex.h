#ifndef mutexh
#define mutexh
#include <linked_list.h>
#include <spinlock.h>

typedef struct mutex_t
{
    list_head_t pendq;
    spinlock_t lock;
    volatile void *owner;
    volatile uint32_t rlevel;
   
}mutex_t;

mutex_t *mtx_create();
int mtx_acquire(mutex_t *sem);
int mtx_release(mutex_t *sem);
#endif