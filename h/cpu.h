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
    void *intc;


}cpu_entry_t;

#define APIC_ID_SMT(x)     ((x)         & 0xF)
#define APIC_ID_CORE(x)    (((x) >> 4)  & 0xF)
#define APIC_ID_MODULE(x)  (((x) >> 8)  & 0xF)
#define APIC_ID_TILE(x)    (((x) >> 16) & 0xF)
#define APIC_ID_DIE(x)     (((x) >> 20) & 0xF)
#define APIC_ID_PACKAGE(x) (((x) >> 24) & 0xF)
#define APIC_ID_CLUSTER(x) (((x) >> 28) & 0xF)

int cpu_init(void);
int cpu_ap_setup(uint32_t cpu_id);
int cpu_get_entry
(
    uint32_t apic_id, 
    cpu_entry_t **cpu_out
);
#endif