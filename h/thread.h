#ifndef threadh
#define threadh

#include <stddef.h>
#include <stdint.h>

void *thread_create
(
    void     (*th_entry)(void *arg),
    void      *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t affinity,
    void      *owner
);

int thread_create_static
(
    void *out_th,
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio,
    cpu_aff_t *affinity,
    void *owner
);

int thread_start
(
    sched_thread_t *th
);

#endif