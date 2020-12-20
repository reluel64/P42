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
    void *context;
}cpu_t;

typedef struct cpu_api_t
{
    uint32_t (*cpu_id_get)(void);
    int  (*cpu_setup)(cpu_t*);
    uint32_t (*cpu_get_domain) (uint32_t cpu_id);
    void (*stack_relocate)(virt_addr_t *new, virt_addr_t *old);
    void (*int_lock)(void);
    void (*int_unlock)(void);
    int (*int_check)(void);
    int (*is_bsp)(void);
    int (*start_ap)(uint32_t num, uint32_t timeout);
    virt_addr_t (*max_virt_addr)(void);
    phys_addr_t (*max_phys_addr)(void);
    int (*ipi_issue)(uint8_t, uint32_t, uint32_t);
    void (*halt) (void);
    void (*pause)(void);
    void (*ctx_save)(virt_addr_t, void* th);
    void (*ctx_restore)(virt_addr_t, void* th);
    void *(*ctx_init)(void *th);
}cpu_api_t;


int cpu_setup(device_t *dev);
uint32_t cpu_id_get(void);
int cpu_int_lock(void);
int cpu_int_unlock(void);
int cpu_int_check(void);
int cpu_init(void);
virt_addr_t cpu_virt_max();
phys_addr_t cpu_phys_max();
int cpu_api_register(void);
int cpu_ap_start(uint32_t count, uint32_t timeout);
void cpu_halt(void);
void cpu_pause(void);
int cpu_issue_ipi
(
    uint8_t dest, 
    uint32_t cpu,
    uint32_t type
);
void cpu_ctx_save(virt_addr_t iframe, void *th);
void cpu_ctx_restore(virt_addr_t iframe, void *th);
void *cpu_ctx_init(void *th);
cpu_t *cpu_current_get(void);

#endif