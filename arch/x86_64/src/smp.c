/* SMP handling code */

#include <vmmgr.h>
#include <linked_list.h>
#include <acpi.h>
#include <utils.h>
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
    uint32_t domain;
    phys_addr_t apic_address;
    virt_addr_t stack;
}cpu_entry_t;

int smp_init(void)
{
    ACPI_STATUS        status = AE_OK;
    ACPI_TABLE_MADT   *madt = NULL;
    ACPI_TABLE_SRAT   *srat = NULL;
    ACPI_SRAT_CPU_AFFINITY *cpu_aff = NULL;
    ACPI_SUBTABLE_HEADER *subhdr = NULL;
    ACPI_MADT_LOCAL_APIC *lapic = NULL;
    
    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("MADT table not available\n");
        return(-1);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); i< madt->Header.Length;i+=subhdr->Length)
    {

        subhdr = (uint8_t*)madt + i;

        if(subhdr->Type != ACPI_MADT_TYPE_LOCAL_APIC)
            continue;
        
        lapic = (ACPI_MADT_LOCAL_APIC*)subhdr;

        kprintf("APIC_ID 0x%x\n",lapic->Id);
    }


    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    kprintf("HELLO\n");

    status = AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat);

    if(ACPI_FAILURE(status))
    {
        kprintf("MADT table not available\n");
        return(-1);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_SRAT); i< srat->Header.Length;i+=subhdr->Length)
    {

        subhdr = (uint8_t*)srat + i;
        
        if(subhdr->Type != ACPI_SRAT_TYPE_CPU_AFFINITY)
            continue;
        
        cpu_aff = (ACPI_SRAT_CPU_AFFINITY*)subhdr;

        kprintf("APIC_ID 0x%x CLOCK 0x%x Proximity %d\n",cpu_aff->ApicId, cpu_aff->ClockDomain, cpu_aff->ProximityDomainLo);
    }

    kprintf("HELLO\n");

}