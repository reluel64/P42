#ifndef schedh
#define schedh
#include <devmgr.h>
#include <cpu.h>

typedef struct sched_thread_t
{
    list_node_t node;
    void       *owner;
    void       *context;
    uint32_t    id;
    uint16_t    prio;
    virt_addr_t stack;
    virt_size_t stack_sz;
    void       *entry_point;
    void *pv;
}sched_thread_t;

int sched_cpu_init(device_t *timer, cpu_t *cpu);
int sched_init(void);
int sched_init_thread
(
    sched_thread_t    *th,
    void        *entry_pt,
    virt_size_t stack_sz,
    uint32_t    prio,
    void *pv
    
);
int shced_start_thread(sched_thread_t *th);
#endif