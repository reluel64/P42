#ifndef threadh
#define threadh

#include <stddef.h>
#include <stdint.h>

int thread_create_static
(
    void *out_th,
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio
);

int thread_start
(
    sched_thread_t *th
);

#endif