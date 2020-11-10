#ifndef cpuh
#define cpuh

#include <linked_list.h>
#include <defs.h>
#include <devmgr.h>

#define CPU_DEVICE_TYPE "cpu"

typedef struct cpu_t
{
    uint32_t cpu_id;
    uint32_t proximity_domain;
    virt_addr_t stack_top;
    virt_addr_t stack_bottom;
    void *cpu_pv;
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
}cpu_api_t;


int cpu_setup(dev_t *dev);
uint32_t cpu_id_get(void);
int cpu_int_lock(void);
int cpu_int_unlock(void);
int cpu_int_check(void);
int cpu_register(void);
#endif