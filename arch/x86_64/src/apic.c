#include <vmmgr.h>
#include <stddef.h>
#include <linked_list.h>
#include <isr.h>
#include <utils.h>
#include <cpu.h>
#include <apic.h>
#include <liballoc.h>
#include <acpi.h>
#include <timer.h>
#include <devmgr.h>
#include <intc.h>
#include <platform.h>
#define LVT_LINT_VECTOR (252)
#define LVT_ERROR_VECTOR (254)
#define SPURIOUS_VECTOR  (255)
#define TIMER_VECTOR      (32)


static int apic_nmi_fill
(
    apic_device_t *apic
)
{
    ACPI_STATUS                 status  = AE_OK;
    ACPI_TABLE_MADT             *madt   = NULL;
    ACPI_SUBTABLE_HEADER        *subhdr = NULL;
    ACPI_MADT_LOCAL_APIC_NMI    *nmi    = NULL;
    ACPI_MADT_LOCAL_X2APIC_NMI  *x2nmi  = NULL;
    int                         found   = 0;

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

        if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_X2APIC_NMI)
        {
            x2nmi = (ACPI_MADT_LOCAL_X2APIC_NMI*)subhdr;

            if ((apic->apic_id == x2nmi->Uid) || 
                (x2nmi->Uid == UINT32_MAX))
            {
                apic->polarity = x2nmi->IntiFlags & ACPI_MADT_POLARITY_MASK;
                apic->trigger = x2nmi->IntiFlags & ACPI_MADT_TRIGGER_MASK;
                apic->lint    = x2nmi->Lint;
                switch(apic->polarity)
                {
                    case ACPI_MADT_POLARITY_CONFORMS:
                    case ACPI_MADT_POLARITY_ACTIVE_LOW:
                    case ACPI_MADT_POLARITY_RESERVED:
                        apic->polarity = 0;
                        break;
                    case ACPI_MADT_POLARITY_ACTIVE_HIGH:
                        apic->polarity = 1;
                        break;
                }

                switch(apic->trigger)
                {
                    case ACPI_MADT_TRIGGER_CONFORMS:
                    case ACPI_MADT_TRIGGER_EDGE:
                    case ACPI_MADT_TRIGGER_RESERVED:
                        apic->trigger = 0;
                        break;
                    case ACPI_MADT_TRIGGER_LEVEL:
                        apic->trigger = 1;
                        break;
                }
                found = 1;
                break;
            }
        }
    }

    if(!found)
    {
        for(phys_size_t i = sizeof(ACPI_TABLE_MADT); 
            i < madt->Header.Length;
            i += subhdr->Length)
        {
            subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

            if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_APIC_NMI)
            {
                nmi = (ACPI_MADT_LOCAL_APIC_NMI*)subhdr;

                if((apic->apic_id == nmi->ProcessorId) || 
                (nmi->ProcessorId == UINT8_MAX))
                {
                    apic->lint     = nmi->Lint;
                    apic->polarity = nmi->IntiFlags & ACPI_MADT_POLARITY_MASK;
                    apic->trigger = nmi->IntiFlags & ACPI_MADT_TRIGGER_MASK;

                    switch(apic->polarity)
                    {
                        case ACPI_MADT_POLARITY_CONFORMS:
                        case ACPI_MADT_POLARITY_ACTIVE_LOW:
                        case ACPI_MADT_POLARITY_RESERVED:
                            apic->polarity = 0;
                            break;
                        case ACPI_MADT_POLARITY_ACTIVE_HIGH:
                            apic->polarity = 1;
                            break;
                    }

                    switch(apic->trigger)
                    {
                        case ACPI_MADT_TRIGGER_CONFORMS:
                        case ACPI_MADT_TRIGGER_EDGE:
                        case ACPI_MADT_TRIGGER_RESERVED:
                            apic->trigger = 0;
                            break;
                        case ACPI_MADT_TRIGGER_LEVEL:
                            apic->trigger = 1;
                            break;
                    }
                    break;
                }

                kprintf("HAZ_NMI %d %d\n",nmi->ProcessorId, nmi->Lint);
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(found ? 0 : -1);
}

static int apic_spurious_handler(void *pv, virt_addr_t iframe)
{
    return(0);
}

static int apic_lvt_error_handler(void *pv, virt_addr_t iframe)
{
    apic_drv_private_t *apic_drv = NULL;
    kprintf("APIC_ERROR_HANDLER\n");
    apic_drv = devmgr_drv_data_get(pv);

    (*apic_drv->reg->esr) = 0;

    kprintf("ERROR %x\n",(*apic_drv->reg->esr));

    return(0);
}

static phys_addr_t apic_phys_addr(void)
{
    phys_addr_t apic_base = 0;
    phys_addr_t max_phys = 0;

    apic_base = __rdmsr(APIC_BASE_MSR);
    max_phys  = cpu_phys_max();

    apic_base &= ~((1 << 12) - 1);

    apic_base &= max_phys;

    return(apic_base);
}

static int apic_send_ipi
(
    device_t *dev,
    ipi_packet_t *ipi
)
{
    driver_t           *drv      = NULL;
    apic_drv_private_t *apic_drv = NULL;

    uint32_t reg_low = 0;
    uint32_t reg_hi  = 0;

    drv = devmgr_dev_drv_get(dev);

    apic_drv = devmgr_drv_data_get(drv);
    
    reg_hi = ((uint32_t)ipi->dest_cpu) << 24;
   
    reg_low = ipi->vector;

    switch(ipi->type)
    {
        case IPI_INIT:
            reg_low = APIC_ICR_DELIVERY_INIT << 8;
            break;
        
        case IPI_START_AP:
            reg_low |= APIC_ICR_DELIVERY_START << 8;
            break;
        
        default:
           reg_low &= ~0b11100000000;
            break;
    }

    if(ipi->dest_mode == IPI_DEST_MODE_LOGICAL)
        reg_low |= (APIC_ICR_DEST_MODE_LOGICAL << 11);

    if(ipi->level == IPI_LEVEL_ASSERT)
        reg_low |= (APIC_ICR_LEVEL_ASSERT << 14);

    if(ipi->trigger == IPI_TRIGGER_LEVEL)
        reg_low |= (APIC_ICR_TRIGGER_LEVEL << 15);
    
    switch(ipi->dest)
    {
        case IPI_DEST_NO_SHORTHAND:
            reg_low |= (APIC_ICR_DEST_SHORTLAND_NO << 18);
            break;

        case IPI_DEST_SELF:
            reg_low |= (APIC_ICR_DEST_SHORTLAND_SELF << 18);
            break;

        case IPI_DEST_ALL:
            reg_low |= (APIC_ICR_DEST_SHORTLAND_ALL_AND_SELF << 18);
            break;

        case IPI_DEST_ALL_NO_SELF:
            reg_low |= (APIC_ICR_DEST_SHORTLAND_ALL_NO_SELF << 18);
            break;
    }

    apic_drv->reg->icr[4] = reg_hi;
    apic_drv->reg->icr[0] = reg_low;

    /* wait for it to be 0 */
    while(apic_drv->reg->icr[0] & APIC_ICR_DELIVERY_STATUS_MASK);

    return(0);
}

static int apic_eoi_handler(void *pv, virt_addr_t iframe)
{
    apic_drv_private_t *apic_drv = NULL;
   
    apic_drv = devmgr_drv_data_get(pv);

    (*apic_drv->reg->eoi) = 0;
    
    return(0);
}

static int apic_probe(device_t *dev)
{
    ACPI_STATUS            status   = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;

    if(!devmgr_dev_name_match(dev, APIC_DRIVER_NAME))
        return(-1);

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        return(-1);
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(0);
}

static int apic_dev_init(device_t *dev)
{
    volatile apic_reg_t *reg      = NULL;
    apic_device_t       *apic     = NULL;
    driver_t            *drv      = NULL;
    apic_drv_private_t  *apic_drv = NULL;
    uint32_t            lint      = 0;
    uint32_t            flags     = 0;
    uint8_t             polarity  = 0;
    uint8_t             trigger   = 0;
    
    cpu_int_lock();
    
    drv = devmgr_dev_drv_get(dev);
    apic_drv = devmgr_drv_data_get(drv);

    apic = kmalloc(sizeof(apic_device_t));

    if(apic == NULL)
        return(-1);

    kprintf("INIT_APIC 0x%x\n", dev);

    apic->apic_id = devmgr_dev_index_get(dev);

    kprintf("APIC_BASE 0x%x APIC ID %d\n",apic_drv->paddr, apic->apic_id);

    devmgr_dev_data_set(dev, apic);
    
    apic_nmi_fill(apic);

    reg = apic_drv->reg;

    /* Stop APIC */
    (*reg->svr)     &= ~APIC_SVR_ENABLE_BIT;

    /* Set up LVT error handling */
    (*reg->lvt_err) &= ~APIC_LVT_INT_MASK;
    (*reg->lvt_err) = APIC_LVT_VECTOR_MASK(LVT_ERROR_VECTOR);

#if 1
    /* Set up LINT */
    if(apic->lint == 0)
    {
        (*reg->lvt_lint0) = LVT_LINT_VECTOR                    | 
                            (((uint32_t)apic->polarity) << 13) | 
                            (((uint32_t)apic->trigger)  << 15);
    }
    else
    {
        (*reg->lvt_lint1) = LVT_LINT_VECTOR                    | 
                            (((uint32_t)apic->polarity) << 13) | 
                            (((uint32_t)apic->trigger)  << 15);

    }           
    #endif
    /* Enable APIC */
    (*reg->svr)     = APIC_SVR_ENABLE_BIT | 
                       APIC_SVR_VEC_MASK(SPURIOUS_VECTOR);

    (*reg->eoi) = 0;
 
    cpu_int_unlock();
   
    return(0);
}

static int apic_drv_init(driver_t *drv)
{
    apic_drv_private_t  *apic_drv = NULL;
    volatile apic_reg_t *reg = NULL;

    __write_cr8(0);
    
    apic_drv = kmalloc(sizeof(apic_drv_private_t));
    
    if(apic_drv == NULL)
        return(-1);

    apic_drv->paddr   = apic_phys_addr();

    apic_drv->reg     = (apic_reg_t*)vmmgr_map(NULL, apic_drv->paddr, 
                                0x0, 
                                sizeof(apic_reg_t), 
                                VMM_ATTR_WRITABLE |
                                VMM_ATTR_STRONG_UNCACHED);

    isr_install(apic_lvt_error_handler, drv, LVT_ERROR_VECTOR,0);
    isr_install(apic_spurious_handler, drv, SPURIOUS_VECTOR,0);
    isr_install(apic_eoi_handler, drv, 0, 1);
    devmgr_drv_data_set(drv, apic_drv);

    return(0);
}

static intc_api_t apic_api = 
{
    .enable   = NULL,
    .disable  = NULL,
    .send_ipi = apic_send_ipi
};

static driver_t apic_drv = 
{
    .drv_name   = APIC_DRIVER_NAME,
    .dev_probe  = apic_probe,
    .dev_init   = apic_dev_init,
    .dev_uninit = NULL,
    .drv_type   = INTERRUPT_CONTROLLER,
    .drv_init   = apic_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &apic_api
};

int apic_register(void)
{
    devmgr_drv_add(&apic_drv);
    devmgr_drv_init(&apic_drv);

    return(0);
}