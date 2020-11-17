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
#define LVT_ERROR_VECTOR (254)
#define SPURIOUS_VECTOR  (63)
#define TIMER_VECTOR      (32)

static int apic_spurious_handler(void *pv, uint64_t error)
{
    return(0);
}

static int apic_lvt_error_handler(void *pv, uint64_t error)
{
    apic_device_t *apic = NULL;
    apic_reg_t *reg = NULL;
    

    kprintf("ERROR %d\n",cpu_id_get());
 #if 0   
    apic = apic_get();

    if(apic == NULL)
        return(-1);

    reg = (apic_reg_t*)apic->reg;
    reg->eoi[0] = 0x1;    
#endif
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
           reg_low &= ~0b1110000000;
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

static int apic_timer(void *pv, uint64_t ec)
{
    apic_drv_private_t *apic_drv = NULL;

    apic_drv = devmgr_drv_data_get(pv);

    (*apic_drv->reg->eoi)=0;

    return(0);
}

static int apic_eoi_handler(void *pv, uint64_t ec)
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
    volatile apic_reg_t *reg = NULL;
    apic_device_t *apic = NULL;
    driver_t *drv = NULL;
    apic_drv_private_t *apic_drv = NULL;

    cpu_int_lock();
    
    drv = devmgr_dev_drv_get(dev);
    apic_drv = devmgr_drv_data_get(drv);

    apic = kmalloc(sizeof(apic_device_t));

    if(apic == NULL)
        return(-1);

    kprintf("INIT_APIC\n");

    apic->apic_id = devmgr_dev_index_get(dev);

    kprintf("APIC_BASE 0x%x APIC ID %d\n",apic_drv->paddr, apic->apic_id);

    devmgr_dev_data_set(dev, apic);
    
    reg = apic_drv->reg;

    /* Stop APIC */
    (*reg->svr)     &= ~APIC_SVR_ENABLE_BIT;

    /* Set up LVT error handling */
    (*reg->lvt_err) &= ~APIC_LVT_INT_MASK;
    (*reg->lvt_err) |= APIC_LVT_VECTOR_MASK(LVT_ERROR_VECTOR);

    /* Enable APIC */
    (*reg->svr)     |= APIC_SVR_ENABLE_BIT | 
                       APIC_SVR_VEC_MASK(SPURIOUS_VECTOR);

    (*reg->eoi) = 0;

    cpu_int_unlock();

    return(0);
}

static int apic_drv_init(driver_t *drv)
{
    apic_drv_private_t  *apic_drv = NULL;
    volatile apic_reg_t *reg = NULL;

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