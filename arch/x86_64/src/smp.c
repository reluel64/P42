/* SMP handling code */

#include <vmmgr.h>
#include <linked_list.h>
#include <acpi.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>

typedef struct
{
    list_head_t enabled;
    list_head_t avail;
    
}smp_t;

static smp_t smp;

#if 0
int smp_init_table(void)
{
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;
    ACPI_TABLE_SRAT        *srat    = NULL;
    ACPI_SRAT_CPU_AFFINITY *cpu_aff = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    ACPI_MADT_LOCAL_APIC  *lapic    = NULL;
    cpu_entry_t            *cpu     = NULL;

    memset(&smp, 0, sizeof(smp_t));

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("MADT table not available\n");
        return(-1);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
        i< madt->Header.Length;
        i+=subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        kprintf("TYPE %d\n", subhdr->Type);

        if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_APIC)
        {
            lapic = (ACPI_MADT_LOCAL_APIC*)subhdr;

            if((lapic->LapicFlags & 0x1) || lapic->LapicFlags & 0x2)
            {
                if(smp_add_cpu_entry(lapic->Id, TRUE, &cpu))
                {
                    kprintf("FAILED to add APIC %d\n",lapic->Id);
                }
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    status = AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat);

    if(ACPI_FAILURE(status))
    {
        kprintf("SRAT table not available\n");
        return(0);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_SRAT); 
        i< srat->Header.Length;
        i+=subhdr->Length)
    {

        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)srat + i);

        if(subhdr->Type != ACPI_SRAT_TYPE_CPU_AFFINITY)
            continue;
        
        cpu_aff = (ACPI_SRAT_CPU_AFFINITY*)subhdr;

        if(smp_get_cpu_entry(cpu_aff->ApicId, TRUE, &cpu) == 0)
        {
            cpu->domain = (uint32_t)cpu_aff->ProximityDomainLo          | 
                          (uint32_t)cpu_aff->ProximityDomainHi[0] << 8  | 
                          (uint32_t)cpu_aff->ProximityDomainHi[1] << 16 |
                          (uint32_t)cpu_aff->ProximityDomainHi[2] << 24;
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)srat);
    
    return(0);
}

#endif