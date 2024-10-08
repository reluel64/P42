#ifndef cpuh
#define cpuh

#include <linked_list.h>
#include <defs.h>
#include <devmgr.h>
#define CPU_DEVICE_TYPE "cpu"

typedef struct cpu_t
{
    device_t *dev;
    uint32_t cpu_id;
    uint32_t proximity_domain;
    void *cpu_pv;
    void *sched;
}cpu_t;


int cpu_setup(device_t *dev);
uint32_t cpu_id_get(void);
int cpu_init(void);
virt_addr_t cpu_virt_max();
phys_addr_t cpu_phys_max();
int cpu_api_register(void);
int cpu_ap_start(uint32_t count, uint32_t timeout);
int cpu_issue_ipi
(
    uint8_t dest, 
    uint32_t cpu,
    uint32_t type
);
cpu_t *cpu_current_get(void);
void cpu_resched(void);
void cpu_signal_on
(
    uint32_t cpu_id
);
#endif