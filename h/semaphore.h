#ifndef semaphoreh
#define semaphoreh
#include <linked_list.h>
#include <spinlock.h>

typedef struct sem_t
{
    list_head_t       pendq;
    spinlock_t        lock;
    volatile uint32_t count;
    uint32_t          max_count;
}sem_t;

sem_t *sem_create(uint32_t init_val, uint32_t max_val);
sem_t *sem_init(sem_t *sem, uint32_t init_val, uint32_t max_count);
int sem_acquire(sem_t *sem, uint32_t wait_ms);
int sem_release(sem_t *sem);
#endif