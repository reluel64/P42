
#include <intc.h>
#include <acpi.h>
#include <defs.h>
#include <devmgr.h>
#include <ioapic.h>
#include <liballoc.h>
#include <vmmgr.h>

static uint32_t ioapic_count(void)
{
    uint32_t count = 0;

    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    ACPI_MADT_IO_APIC      *ioapic  = NULL;

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("FAIL\n");
        return(0);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
        i< madt->Header.Length;
        i+=subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if((subhdr->Type == ACPI_MADT_TYPE_IO_APIC) || 
           (subhdr->Type == ACPI_MADT_TYPE_IO_SAPIC))
        {
            count++;
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(count);
}

static int ioapic_probe(dev_t *dev)
{
    if(!devmgr_dev_name_match(dev, IOAPIC_DRV_NAME))
        return(-1); 

    if(ioapic_count() == 0)
        return(-1);

    return(0);
}



static int ioapic_init(dev_t *dev)
{
    uint32_t                dev_index    = 0;
    uint32_t                ioapic_index = 0;
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    ACPI_MADT_IO_APIC      *ioapic  = NULL;
    ioapic_t               *ioapic_dev = NULL;

    dev_index = devmgr_dev_index_get(dev);

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("FAIL\n");
        return(-1);
    }
    
    /* Find the corresponding index */
    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
        i< madt->Header.Length;
        i+=subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if((subhdr->Type != ACPI_MADT_TYPE_IO_APIC) &&
           (subhdr->Type != ACPI_MADT_TYPE_IO_SAPIC))
        {
            continue;
        }
        
        if(dev_index == ioapic_index)
        {
            ioapic = (ACPI_MADT_IO_APIC*)subhdr;
            break;
        }

        ioapic_index ++;

    }

    ioapic_dev = kcalloc(sizeof(ioapic_t), 1);

    if(ioapic_dev == NULL)
    {
        AcpiPutTable((ACPI_TABLE_HEADER*)madt);
        return(-1);
    }

    
    ioapic_dev->irq_base  = ioapic->GlobalIrqBase;
    ioapic_dev->phys_base = ioapic->Address;
    ioapic_dev->id        = ioapic->Id;

    ioapic_dev->virt_base = (virt_addr_t)vmmgr_map(NULL, ioapic_dev->phys_base,
                                      0,    PAGE_SIZE, 
                                      VMM_ATTR_NO_CACHE      | 
                                      VMM_ATTR_WRITE_THROUGH | 
                                      VMM_ATTR_WRITABLE
                                      );
   
    if(ioapic_dev->virt_base == 0)
    {
        kfree(ioapic_dev);
        AcpiPutTable((ACPI_TABLE_HEADER*)madt);
        return(-1);

    }

    ioapic_dev->ioregsel = (volatile ioregsel_t*)ioapic_dev->virt_base;
    ioapic_dev->iowin = (volatile iowin_t*)(ioapic_dev->virt_base + 0x10);

    ioapic_dev->ioregsel->reg_address = 0x1;
    ver = ioapic_dev->iowin->reg_data;
    kprintf("VERSION %x\n", ver & 0xff);

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);
    
    return(0);
}

static int ioapic_drv_init(drv_t *drv)
{
    uint32_t ioapic_cnt = 0;
    dev_t    *dev       = NULL;

    ioapic_cnt = ioapic_count();

    for(uint32_t index = 0; index < ioapic_cnt; index++)
    {
        dev = NULL;
        
        if(devmgr_dev_create(&dev) == 0)
        {
            devmgr_dev_name_set(dev, IOAPIC_DRV_NAME);
            devmgr_dev_type_set(dev, INTERRUPT_CONTROLLER);
            devmgr_dev_index_set(dev, index);
            
            if(devmgr_dev_add(dev, NULL) != 0)
                return(-1);
        }
    }

    return(0);
}


static intc_api_t api = 
{
    .enable = NULL,
    .disable = NULL,
    .send_ipi = NULL
};

static drv_t ioapic_drv = 
{
    .dev_probe  = ioapic_probe,
    .dev_init   = ioapic_init,
    .dev_uninit = NULL,
    .drv_init   = ioapic_drv_init,
    .drv_uninit = NULL,
    .drv_name   = IOAPIC_DRV_NAME,
    .drv_type   = INTERRUPT_CONTROLLER,
    .drv_api    = &api
};


int ioapic_register(void)
{
    devmgr_drv_add(&ioapic_drv);
    devmgr_drv_init(&ioapic_drv);
}
