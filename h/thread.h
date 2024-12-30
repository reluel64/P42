#ifndef threadh
#define threadh

#include <stddef.h>
#include <stdint.h>

typedef void *(*th_entry_point_t)(void *);

void *thread_create
(
    char          *name,
    th_entry_point_t entry_pt,
    void      *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affinity,
    void      *owner
);

int thread_create_static
(
    void *out_th,
    char          *name,
    th_entry_point_t entry_pt,
    void *arg,
    size_t stack_sz,
    uint32_t prio,
    cpu_aff_t *affinity,
    void *owner
);

int thread_start
(
    struct sched_thread *th
);

void *kthread_create
(
    char          *name,
    th_entry_point_t entry_pt,
    void *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affinity
);

int kthread_create_static
(
    void *out_th,
    char          *name,
    th_entry_point_t entry_pt,
    void *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affinity
);

#endif