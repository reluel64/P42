#ifndef semaphoreh
#define semaphoreh
#include <linked_list.h>
#include <spinlock.h>

typedef struct semb_t
{
    list_head_t pendq;
    spinlock_t lock;
    volatile int flag;
}semb_t;

semb_t *sem_create(int init_val);
int semb_wait(semb_t *sem);
int semb_give(semb_t *sem);
#endif