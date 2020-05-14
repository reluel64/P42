/* SMP handling code */

#include <vmmgr.h>
#include <linked_list.h>
#include <acpi.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>

int smp_start_cpus(void)
{
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;
    ACPI_TABLE_SRAT        *srat    = NULL;
    ACPI_SRAT_CPU_AFFINITY *cpu_aff = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    ACPI_MADT_LOCAL_APIC  *lapic    = NULL;
    cpu_entry_t            *cpu     = NULL;


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

        if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_APIC)
        {
            lapic = (ACPI_MADT_LOCAL_APIC*)subhdr;

            if((lapic->LapicFlags & 0x1) || lapic->LapicFlags & 0x2)
            {
                
                cpu_ap_setup(lapic->Id);
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);
    return(0);
}
