#ifndef cpuh
#define cpuh

#include <linked_list.h>
#include <defs.h>

typedef struct cpu_entry_t
{
    
    list_node_t node;
    uint32_t apic_id;
    uint32_t proximity_domain;
    phys_addr_t apic_address;
    virt_addr_t stack_top;
    virt_addr_t stack_bottom;
}cpu_entry_t;

#define APIC_ID_SMT(x)     ((x)         & 0xF)
#define APIC_ID_CORE(x)    (((x) >> 4)  & 0xF)
#define APIC_ID_MODULE(x)  (((x) >> 8)  & 0xF)
#define APIC_ID_TILE(x)    (((x) >> 16) & 0xF)
#define APIC_ID_DIE(x)     (((x) >> 20) & 0xF)
#define APIC_ID_PACKAGE(x) (((x) >> 24) & 0xF)
#define APIC_ID_CLUSTER(x) (((x) >> 28) & 0xF)

int cpu_init(void);

#endif