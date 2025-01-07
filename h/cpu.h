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

struct cpu_api
{
    phys_addr_t (*get_phys_max)(void);
    virt_addr_t (*get_virt_max)(void);
    int32_t (*send_ipi)(uint32_t dest_mode, uint32_t cpu, uint32_t ipi_no);
    
};


struct cpu
{
    struct device_node dev;
    struct ipi_exec_node exec_slots[CPU_IPI_EXEC_NODE_COUNT];
    struct list_head ipi_cb_slots;
    struct list_head avail_ipi_cb_slots;
    struct spinlock  ipi_cb_lock;
    uint32_t cpu_id;
    uint32_t proximity_domain;
    void *cpu_pv;
    void *sched;
};


uint32_t cpu_id_get
(
    void
);

int cpu_init
(
    void
);

virt_addr_t cpu_virt_max
(
    void
);

phys_addr_t cpu_phys_max
(
    void
);

int cpu_ap_start
(
    uint32_t count, 
    uint32_t timeout
);

struct cpu *cpu_current_get
(
    void
);

void cpu_signal_on
(
    void
);

int cpu_issue_ipi
(
    uint32_t dest,
    uint32_t cpu,
    uint32_t vector
);

#endif