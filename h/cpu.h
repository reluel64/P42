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
    virt_addr_t stack_top;
    virt_addr_t stack_bottom;
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
void cpu_ctx_save(virt_addr_t iframe, void *th);
void cpu_ctx_restore(virt_addr_t iframe, void *th);
void *cpu_ctx_init(void *th, void *exec_pt, void *exec_pv);
int cpu_ctx_destroy(void *th);
cpu_t *cpu_current_get(void);
void cpu_resched(void);
void cpu_signal_on(uint32_t cpu);
#endif