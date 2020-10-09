
#include <intc.h>
#include <acpi.h>
#include <defs.h>
#include <ioapic.h>

static int ioapic_probe(void)
{
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    ACPI_MADT_IO_APIC      *ioapic  = NULL;

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("FAIL\n");
        return(-1);
    }

    status = -1;

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
        i< madt->Header.Length;
        i+=subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if((subhdr->Type == ACPI_MADT_TYPE_IO_APIC) || 
           (subhdr->Type == ACPI_MADT_TYPE_IO_SAPIC))
        {
            status = 0;
            break;
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(status);
}

static int ioapic_init(void)
{
    
}