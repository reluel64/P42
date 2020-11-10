
#include <intc.h>
#include <acpi.h>
#include <defs.h>
#include <devmgr.h>
#include <ioapic.h>
#include <liballoc.h>
#include <vmmgr.h>
#include <utils.h>
#include <pic8259.h>

#define IOAPIC_INTPOL_ACTIVE_HIGH (0x0)
#define IOAPIC_INTPOL_ACTIVE_LOW  (0x1)

#define IOAPIC_TRIGGER_EDGE       (0x0)
#define IOAPIC_TRIGGER_LEVEL      (0x1)

#define IRQ0                      (0x20)

typedef struct ioapic_iter_cb_data_t
{
    uint32_t ioapic_id;
    phys_addr_t addr;
    phys_size_t irq_base;
}ioapic_iter_cb_data_t;

typedef int (*ioapic_iter_cb)(ioapic_iter_cb_data_t *cb, void *pv);

static int ioapic_iterate
(
    ioapic_iter_cb cb,
    void *pv
)
{
    uint8_t                 has_ioapic     = 0;
    uint8_t                 has_iosapic    = 0;
    ACPI_STATUS             status         = AE_OK;
    ACPI_TABLE_MADT        *madt           = NULL;
    ACPI_SUBTABLE_HEADER   *ioapic_subhdr  = NULL;
    ACPI_SUBTABLE_HEADER   *iosapic_subhdr = NULL;
    ACPI_MADT_IO_APIC      *ioapic         = NULL;
    ACPI_MADT_IO_SAPIC     *iosapic        = NULL;
    ioapic_iter_cb_data_t  cb_data;
    int                    stop            = 0;

    if(cb == NULL)
        return(-1);

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("FAIL\n");
        return(0);
    }

    /* 
     * Documentation says:
     * 
     * The I/O SAPIC structure is very similar to the I/O APIC structure. 
     * If both I/O APIC and I/O SAPIC structures exist for a specific APIC ID, 
     * the information in the I/O SAPIC structure must be used.
     * 
     * Ok, let's do that by going through
     * the IOAPIC table and then checking if 
     * SAPIC exists for the same APIC ID
     * 
     */ 

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
        i < madt->Header.Length;
        i += ioapic_subhdr->Length)
    {
        ioapic_subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if(ioapic_subhdr->Type == ACPI_MADT_TYPE_IO_APIC)
        {
            has_ioapic = 1;
            has_iosapic = 0;
            ioapic = (ACPI_MADT_IO_APIC*)ioapic_subhdr;

            for(phys_size_t j = sizeof(ACPI_TABLE_MADT);
                j < madt->Header.Length;
                j += iosapic_subhdr->Length)
            {
                iosapic_subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + j);

                if(iosapic_subhdr->Type == ACPI_MADT_TYPE_IO_SAPIC)
                {
                    iosapic = (ACPI_MADT_IO_SAPIC*)iosapic_subhdr;

                    if(iosapic->Id == ioapic->Id)
                    {
                        vga_print("IOSAPIC_CHECK\n",0x7,-1);
                        has_iosapic = 1;
                        cb_data.ioapic_id = iosapic->Id;
                        cb_data.irq_base  = iosapic->GlobalIrqBase;
                        cb_data.addr      = iosapic->Address;

                        stop = cb(&cb_data, pv);
                        break;
                    }
                }
            }

            if(!has_iosapic && !stop)
            {
                cb_data.ioapic_id = ioapic->Id;
                cb_data.irq_base  = ioapic->GlobalIrqBase;
                cb_data.addr      = ioapic->Address;

                stop = cb(&cb_data, pv);
                vga_print("NO_IOSAPIC\n",0x7,-1);
            }

            if(stop)
            {
                break;
            }
        }
    }

    /* If there is no IOAPIC entry found,
     * then try only SAPIC, just in case
     * the platform does have only SAPIC entries
     */

    if(!has_ioapic && !stop)
    {
        for(phys_size_t j = sizeof(ACPI_TABLE_MADT);
            j < madt->Header.Length;
            j += iosapic_subhdr->Length)
        {
            iosapic_subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + j);

            if(iosapic_subhdr->Type == ACPI_MADT_TYPE_IO_SAPIC)
            {
                iosapic = (ACPI_MADT_IO_SAPIC*)iosapic_subhdr;
                cb_data.ioapic_id = iosapic->Id;
                cb_data.irq_base  = iosapic->GlobalIrqBase;
                cb_data.addr      = iosapic->Address;

                stop = cb(&cb_data, pv);
                vga_print("IOSAPIC_CHECK2\n",0x7,-1);
            }

            if(stop)
            {
                break;
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    if(stop < 0)
    {
        return(-1);
    }

    return(0);
}

static int ioapic_write
(
    dev_t *dev, 
    uint32_t reg,
    void *data, 
    size_t length
)
{
    ioapic_t *ioapic = NULL;
    uint32_t *iowin_data = NULL;
    
    ioapic = devmgr_dev_data_get(dev);

    if(ioapic == NULL)
        return(-1);

    reg = reg & 0xff;
    iowin_data = data;

    ioapic->ioregsel->reg_address = reg;
    ioapic->iowin->reg_data = iowin_data[0];

    /* Wait, there's more */
    if(length > sizeof(uint32_t))
    {
        ioapic->ioregsel->reg_address = reg +1 ;
        ioapic->iowin->reg_data       = iowin_data[1];
    }

    return(0);
}

static int ioapic_read
(
    dev_t *dev, 
    uint32_t reg,
    void *data, 
    size_t length
)
{
    ioapic_t *ioapic = NULL;
    uint32_t *iowin_data = NULL;
    
    ioapic = devmgr_dev_data_get(dev);

    if(ioapic == NULL)
        return(-1);
    
    reg = reg & 0xff;
    iowin_data = data;
    ioapic->ioregsel->reg_address = reg;
    iowin_data[0] = ioapic->iowin->reg_data;

    /* Wait, there's more */
    if(length > sizeof(uint32_t))
    {
        ioapic->ioregsel->reg_address = reg + 1;
        iowin_data[1] = ioapic->iowin->reg_data;
    }

    return(0);
}

static int ioapic_count
(
    ioapic_iter_cb_data_t *entry,
    void *pv
)
{
    uint32_t *count = pv;

    (*count)++;

    return(0);
}

static int ioapic_redirect_vector
(
    uint32_t *vector,
    uint16_t *flags
)
{
    ACPI_STATUS                    status   = AE_OK;
    ACPI_TABLE_MADT               *madt     = NULL;
    ACPI_SUBTABLE_HEADER          *subhdr   = NULL;
    ACPI_MADT_INTERRUPT_OVERRIDE  *override = NULL;
    int                    found            = 0;

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("FAIL\n");
        return(0);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
        i < madt->Header.Length;
        i += subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if(subhdr->Type == ACPI_MADT_TYPE_INTERRUPT_OVERRIDE)
        {
            override = (ACPI_MADT_INTERRUPT_OVERRIDE*)subhdr;
            if(override->SourceIrq == (*vector))
            {

                kprintf("REDIRECTING %d to %d\n",(*vector), override->GlobalIrq);
                (*vector) = override->GlobalIrq;
                (*flags)  = override->IntiFlags;
                found = 1;
                break;
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(found ? 0 : -1);
}

static int ioapic_device_init
(
    ioapic_iter_cb_data_t *entry,
    void *pv
)
{
    void      **cb_data  = NULL;
    dev_t      *dev      = NULL;
    uint32_t   *index    = NULL;
    ioapic_t   *ioapic   = NULL;
    uint32_t   dev_index = 0;
    uint32_t   vector    = 0;
    uint32_t   irq       = 0;
    uint16_t   int_flags = 0;
    uint8_t    polarity  = 0;
    uint8_t    trigger    = 0;
    ioredtbl_t *tbl = NULL;
    ioredtbl_t temp_tbl;
    ioapic_ver_t version;
    uint8_t    redir_tbl_count = 0;
    uint32_t   ioapic_tbl_index = 0;
    uint32_t   redir_vector     = 0;

    cb_data = (void**)pv;
    dev     = (dev_t*)cb_data[0];
    index   = (uint32_t*)cb_data[1];

    dev_index = devmgr_dev_index_get(dev);

    /* Check if this is our index */
    if(dev_index != (*index))
    {
        (*index)++;
        return(0);
    }

    /* Allocate memory for per-device data */
    ioapic = kcalloc(sizeof(ioapic_t), 1);

    if(ioapic == NULL)
    {
        return(-1);
    }

    /* populate ioapic structure */
    ioapic->irq_base  = entry->irq_base;
    ioapic->phys_base = entry->addr;
    ioapic->id        = entry->ioapic_id;
    
    /* Map registers into virtual memory */
    ioapic->virt_base = (virt_addr_t)vmmgr_map(NULL, ioapic->phys_base,
                                      0,    PAGE_SIZE, 
                                      VMM_ATTR_STRONG_UNCACHED |
                                      VMM_ATTR_WRITABLE
                                      );
   
    if(ioapic->virt_base == 0)
    {
        kfree(ioapic);
        return(-1);

    }
    
    /* save pointers of IOREGSEL and IOWIN */
    ioapic->ioregsel = (volatile ioregsel_t*)ioapic->virt_base;
    ioapic->iowin = (volatile iowin_t*)(ioapic->virt_base + 0x10);
    
    /* Save per device structure */
    devmgr_dev_data_set(dev, ioapic);

    memset(&version, 0, sizeof(ioapic_ver_t));

    ioapic_read(dev, 0x1, &version, sizeof(ioapic_ver_t));

    ioapic->redir_tbl_count = version.max_redir;
    kprintf("IRQ BASE %d\n",ioapic->irq_base);
  
    tbl = kcalloc(sizeof(ioredtbl_t), ioapic->redir_tbl_count);

    if(tbl == NULL)
    {
        vmmgr_unmap(NULL, ioapic->virt_base, PAGE_SIZE);
        kfree(ioapic);
        return(-1);
    }

    /* set up interrupt vectors */
    for(uint32_t i = 0; i < ioapic->redir_tbl_count;i++)
    {
        vector = i + ioapic->irq_base;
        tbl[i].intvec = vector;
    }

    for(uint32_t i = 0; i < ioapic->redir_tbl_count; i++)
    {
        vector = i + ioapic->irq_base;
        redir_vector = vector;
        int_flags = 0;

        ioapic_redirect_vector(&redir_vector, &int_flags);
        
        polarity = (int_flags & 0x3);
        trigger = (int_flags >> 2) & 0x3;

        switch(polarity)
        {
            default:
            case 0:
            case 1:
                tbl[i].intpol = IOAPIC_INTPOL_ACTIVE_HIGH;
            break;

            case 3:
                tbl[i].intpol = IOAPIC_INTPOL_ACTIVE_LOW;
                break;
        }

        switch(trigger)
        {
            default:
            case 0:
            case 1:
                tbl[i].tr_mode = IOAPIC_TRIGGER_EDGE;
            break;

            case 3:
                tbl[i].tr_mode = IOAPIC_TRIGGER_LEVEL;
                break;
        }

        /* Ok, we must redirect interrupts */
        if(redir_vector != vector)
        {
            temp_tbl = tbl[i];
            tbl[i] = tbl[redir_vector - ioapic->irq_base];
            tbl[redir_vector - ioapic->irq_base] = temp_tbl;
        }

    }

    for(uint32_t i = 0; i < ioapic->redir_tbl_count; i++)
    {
        tbl[i].intvec += IRQ0;
        ioapic_write(dev, 0x10 + i * 2, tbl + i, sizeof(ioredtbl_t));
    }

    return(1);
}

static int ioapic_probe(dev_t *dev)
{
    uint32_t count = 0;
   
    if(!devmgr_dev_name_match(dev, IOAPIC_DRV_NAME) ||
       !devmgr_dev_type_match(dev, INTERRUPT_CONTROLLER))
        return(-1); 

    ioapic_iterate(ioapic_count, &count);

    if(count == 0)
        return(-1);

    return(0);
}

static int ioapic_init(dev_t *dev)
{
    int     status         = 0;
    void    *cb_data[2]    = {NULL, NULL};
    uint32_t index         = 0;

    cb_data[0] = dev;
    cb_data[1] = &index;

    status = ioapic_iterate(ioapic_device_init, 
                            (void*)cb_data);

    return(status);
}

static int ioapic_drv_init(drv_t *drv)
{
    uint32_t ioapic_cnt = 0;
    dev_t    *dev       = NULL;
    

    ioapic_iterate(ioapic_count, &ioapic_cnt);
    
    for(uint32_t index = 0; index < ioapic_cnt; index++)
    {
        dev = NULL;
        
        if(devmgr_dev_create(&dev) == 0)
        {
            devmgr_dev_name_set(dev, IOAPIC_DRV_NAME);
            devmgr_dev_type_set(dev, INTERRUPT_CONTROLLER);
            devmgr_dev_index_set(dev, index);
            
            if(devmgr_dev_add(dev, NULL) != 0)
            {
                /*devmgr_dev_delete(dev);*/
                return(-1);
            }
        }
    }

    /* We are using I/O APIC so PIC must be disabled */
    dev = devmgr_dev_get_by_name(PIC8259_DRIVER_NAME, 0);

    intc_disable(dev);

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
