#ifndef cpuh
#define cpuh

#include <linked_list.h>
#include <defs.h>
#include <devmgr.h>
#define CPU_DEVICE_TYPE "cpu"
#define CPU_IPI_EXEC_NODE_COUNT 64



struct ipi_exec_node
{
    struct list_node node;
    int32_t (*ipi_handler)(void *pv);
    void *pv;
};

struct cpu_api_t
{

};


struct cpu
{
    struct device_node *dev;
    struct ipi_exec_node exec_nodes[CPU_IPI_EXEC_NODE_COUNT];
    struct list_head ipi_cb;
    struct list_head avail_ipi_cb;
    struct spinlock  ipi_cb_lock;
    uint32_t cpu_id;
    uint32_t proximity_domain;
    void *cpu_pv;
    void *sched;
};


uint32_t cpu_id_get(void);
int cpu_init(void);
virt_addr_t cpu_virt_max();
phys_addr_t cpu_phys_max();
int cpu_ap_start(uint32_t count, uint32_t timeout);
int cpu_issue_ipi
(
    uint8_t dest, 
    uint32_t cpu,
    uint32_t type
);
struct cpu *cpu_current_get(void);
void cpu_resched(void);
void cpu_signal_on
(
    uint32_t cpu_id
);
#endif