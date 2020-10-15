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

#define LVT_ERROR_VECTOR (238)
#define SPURIOUS_VECTOR  (239)
#define TIMER_VECTOR      (32)

extern uint64_t __read_apic_base(void);
extern void     __write_apic_base(uint64_t base);
extern uint8_t  __check_x2apic(void);
extern uint64_t  __max_physical_address();


static uint32_t apic_id_get(void);


extern void __cpuid
(
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
);

static int apic_probe(dev_t *dev)
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

static uint32_t apic_id_get(void)
{
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    eax = 0x1F;

    __cpuid(&eax, &ebx, &edx, &ecx);
    kprintf("EAX = 0x1F\n");

    if(eax != 0 || ebx != 0 || ecx != 0 || edx != 0)
        return(edx);
    
    eax = 0xB;
    ecx = 0;

    __cpuid(&eax, &ebx, &edx, &ecx);
    kprintf("EAX = 0xB\n");

    if(eax != 0 || ebx != 0 || ecx != 0 || edx != 0)
        return(edx);

    eax = 0x1;
    ecx = 0;

    __cpuid(&eax, &ebx, &edx, &ecx);
    kprintf("EAX = 0x1\n");
    return((ebx >> 24) & 0xFF);
}

static int apic_spurious_handler(void *pv, uint64_t error)
{
    return(0);
}

static int apic_lvt_error_handler(void *pv, uint64_t error)
{
    apic_dev_t *apic = NULL;
    cpu_entry_t *cpu = NULL;
    apic_reg_t *reg = NULL;
    
    cpu = cpu_get();
 #if 0   
    apic = apic_get();

    if(apic == NULL)
        return(-1);

    reg = (apic_reg_t*)apic->reg;
    reg->eoi[0] = 0x1;    
#endif
    return(0);
}

uint64_t apic_get_phys_addr(void)
{
    uint64_t apic_base = 0;
    uint64_t max_phys_mask = 0;

    apic_base = __read_apic_base();
    max_phys_mask =  __max_physical_address();

    max_phys_mask -= 1;
    max_phys_mask =  ((1ull << max_phys_mask) - 1);

    apic_base &= ~(uint64_t)0xFFF;
    apic_base &= max_phys_mask;

   return(apic_base);
}


static int apic_send_ipi
(
    dev_t *dev,
    ipi_packet_t *ipi
)
{
   // apic_dev_t *apic = dev->dev_data;
   // apic_reg_t *reg = NULL;


#if 0
    reg = apic->reg;
    reg->icr[4] = ipi->high;
    reg->icr[0] = ipi->low;
   
    /* Read back the value to get the status */
    ipi->low = reg->icr[0];
#endif
    return(0);
}

static int apic_cpu_init(dev_t *dev)
{
    kprintf("initializing instance\n");
    volatile apic_reg_t *reg = NULL;
    apic_dev_t *apic = NULL;

    apic = kmalloc(sizeof(apic_dev_t));

    if(apic == NULL)
        return(-1);

    kprintf("INIT_APIC\n");

    apic->paddr   = apic_get_phys_addr();
    apic->apic_id = apic_id_get();
    apic->reg     = (apic_reg_t*)vmmgr_map(NULL, apic->paddr, 0x0, 
                                sizeof(apic_reg_t), 
                                VMM_ATTR_WRITABLE |
                                VMM_ATTR_NO_CACHE |
                                VMM_ATTR_WRITE_THROUGH);
    if(apic->reg == NULL)
    {
        kfree(apic);
        return(-1);
    }

    devmgr_dev_data_set(dev, apic);
    
    reg = apic->reg;

    /* Stop APIC */
    reg->svr[0]     &= ~APIC_SVR_ENABLE_BIT;

    /* Set up LVT error handling */
    reg->lvt_err[0] &= ~APIC_LVT_INT_MASK;
    reg->lvt_err[0] |= APIC_LVT_VECTOR_MASK(LVT_ERROR_VECTOR);

    /* Enable APIC */
    reg->svr[0]     |= APIC_SVR_ENABLE_BIT | 
                       APIC_SVR_VEC_MASK(SPURIOUS_VECTOR);

    return(0);
}

static int apic_drv_init(drv_t *drv)
{
    apic_drv_private_t *apic_pv = NULL;

    apic_pv = kmalloc(sizeof(apic_dev_t));

    if(apic_pv == NULL)
        return(-1);

    
    isr_install(apic_lvt_error_handler, drv, LVT_ERROR_VECTOR);
    isr_install(apic_spurious_handler, drv, SPURIOUS_VECTOR);

    devmgr_drv_data_set(drv, apic_pv);
    
    return(0);
}

static intc_api_t apic_api = 
{
    .enable = NULL,
    .disable = NULL,
    .send_ipi = apic_send_ipi
};

static drv_t apic_drv = 
{
    .drv_name  = APIC_DRIVER_NAME,
    .dev_probe = apic_probe,
    .dev_init   = apic_cpu_init,
    .dev_uninit = NULL,
    .drv_type  = INTERRUPT_CONTROLLER,
    .drv_init   = apic_drv_init,
    .drv_uninit = NULL,
    .drv_api = &apic_api
};

int apic_register(void)
{
    devmgr_drv_add(&apic_drv);
    kprintf("%s %d\n",__FUNCTION__,__LINE__);
    devmgr_drv_init(&apic_drv);
    return(0);
}