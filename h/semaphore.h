#ifndef semaphoreh
#define semaphoreh
#include <linked_list.h>
#include <spinlock.h>

typedef struct semaphore_t
{
    list_head_t pendq;
    spinlock_t lock;
    volatile int count;
   
}semaphore_t;

semaphore_t *sem_create(uint32_t init_val);
int sem_acquire(semaphore_t *sem, uint32_t wait_ms);
int sem_release(semaphore_t *sem);
#endif