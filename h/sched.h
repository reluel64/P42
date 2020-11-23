#ifndef schedh
#define schedh
#include <devmgr.h>
#include <cpu.h>

int sched_cpu_init(device_t *timer, cpu_t *cpu);
int sched_init(void);
#endif