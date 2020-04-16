/* SMP handling code */

#include <vmmgr.h>
#include <linked_list.h>
#include <acpi.h>

typedef struct
{
    
    list_node_t node;
    uint32_t cluster_id;
    uint32_t package_id;
    uint32_t die_id;
    uint32_t tile_id;
    uint32_t module_id;
    uint32_t core_id;
    uint32_t smt_id;
    uint32_t apic_id;
    phys_addr_t apic_address;
    virt_addr_t stack;
}cpu_entry_t;

int smp_init(void)
{

}