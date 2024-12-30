#ifndef semaphoreh
#define semaphoreh
#include <linked_list.h>
#include <spinlock.h>

struct sem
{
    struct list_head       pendq;
    struct spinlock        lock;
    volatile uint32_t count;
    uint32_t          max_count;
};

struct sem *sem_create
(
    uint32_t init_val, 
    uint32_t max_val
);

struct sem *sem_init
(
    struct sem *sem, 
    uint32_t init_val, 
    uint32_t max_count
);

int sem_acquire
(
    struct sem *sem, 
    uint32_t wait_ms
);

int sem_release
(
    struct sem *sem
);
#endif