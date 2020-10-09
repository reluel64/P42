#ifndef cpuh
#define cpuh

#include <linked_list.h>
#include <defs.h>


typedef struct cpu_entry_t
{
    list_node_t node;
    uint32_t cpu_id;
    uint32_t proximity_domain;
    virt_addr_t stack_top;
    virt_addr_t stack_bottom;
    
}cpu_entry_t;

typedef struct cpu_funcs_t
{
    uint32_t (*cpu_id_get)(void);
    int  (*cpu_setup)(cpu_entry_t*);

    
}cpu_funcs_t;

int cpu_init(void);
int cpu_ap_setup(uint32_t cpu_id);
int cpu_get_current(cpu_entry_t **cpu_out);
int cpu_register_funcs(cpu_funcs_t *func);
cpu_entry_t *cpu_get(void);
uint32_t cpu_id_get(void);
int cpu_get_entry
(
    uint32_t apic_id, 
    cpu_entry_t **cpu_out
);
#endif