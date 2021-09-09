#include <vm.h>
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

static isr_t lint_isr;
static isr_t error_isr;
static isr_t spur_isr;
static isr_t eoi_isr;

static int xapic_write
(
    virt_addr_t    reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
);

static int xapic_read
(
    virt_addr_t   reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
);

static int x2apic_write
(
    virt_addr_t    reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
);

static int x2apic_read
(
    virt_addr_t   reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
);


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
                (x2nmi->Uid    == UINT32_MAX))
            {
                apic->polarity = x2nmi->IntiFlags & ACPI_MADT_POLARITY_MASK;
                apic->trigger  = x2nmi->IntiFlags & ACPI_MADT_TRIGGER_MASK;
                apic->lint     = x2nmi->Lint;

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

static int apic_spurious_handler(void *pv, isr_info_t *inf)
{
    return(0);
}

static int apic_lvt_error_handler(void *pv, isr_info_t *inf)
{
    apic_drv_private_t *apic_drv = NULL;
    uint32_t            data = 0;

    kprintf("APIC_ERROR_HANDLER\n");
    apic_drv = devmgr_drv_data_get(pv);

    apic_drv->apic_write(apic_drv->vaddr, 
                        ERROR_STATUS_REGISTER, 
                        &data, 
                        1);
    return(0);
}

static int apic_eoi_handler(void *pv, isr_info_t *inf)
{
    apic_drv_private_t *apic_drv = NULL;
    uint32_t            data     = 0;
    
    apic_drv = devmgr_drv_data_get(pv);

    apic_drv->apic_write(apic_drv->vaddr, 
                        EOI_REGISTER, 
                        &data, 
                        1);
    
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

static void apic_enable_x2(void)
{
    phys_addr_t apic_base = 0;

    apic_base = __rdmsr(APIC_BASE_MSR);

    apic_base |= (1 << 10);

    __wrmsr(APIC_BASE_MSR, apic_base);
}

static uint8_t apic_has_x2(void)
{
    uint32_t eax = 1;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    __cpuid(&eax, &ebx, &ecx, &edx);

    ecx >>= 21;

    return(ecx & 0x1);
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
    uint32_t data[2] = {0,0};

    drv = devmgr_dev_drv_get(dev);

    if(drv == NULL)
        return(-1);

    apic_drv = devmgr_drv_data_get(drv);
    
    if(apic_drv == NULL)
        return(-1);

    reg_hi = ipi->dest_cpu;
   
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

    data[0] = reg_low;
    data[1] = reg_hi;

    apic_drv->apic_write(apic_drv->vaddr, 
                         INTERRUPT_COMMAND_REGISTER, 
                         data, 
                         2);
    
    /* Poll for IPI delivery status */
    do
    {
        apic_drv->apic_read(apic_drv->vaddr, 
                            INTERRUPT_COMMAND_REGISTER, 
                            data, 
                            2);

    }while(data[0] & APIC_ICR_DELIVERY_STATUS_MASK);


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
    apic_device_t       *apic     = NULL;
    driver_t            *drv      = NULL;
    apic_drv_private_t  *apic_drv = NULL;
    uint32_t            lint      = 0;
    uint32_t            flags     = 0;
    uint8_t             polarity  = 0;
    uint8_t             trigger   = 0;
    uint32_t            data      = 0;
    uint64_t            apic_base = 0;
    int                 int_status = 0;

    kprintf("APIC_DEV_INIT\n");

    int_status = cpu_int_check();

    if(int_status)
        cpu_int_lock();
    
    drv = devmgr_dev_drv_get(dev);
    apic_drv = devmgr_drv_data_get(drv);

    apic = kcalloc(sizeof(apic_device_t), 1);

    if(apic == NULL)
    {
        if(int_status)
        {
            cpu_int_unlock();
        }

        return(-1);
    }
        
    
    /* If x2APIC is supported, then enable it */
    if(apic_drv->x2)
    {
        kprintf("USING X2APIC\n");
        apic_enable_x2();
    }   
    
    kprintf("INIT_APIC 0x%x\n", dev);

    apic->apic_id = devmgr_dev_index_get(dev);

    kprintf("APIC_BASE 0x%x APIC ID %d\n",apic_drv->paddr, apic->apic_id);

    devmgr_dev_data_set(dev, apic);

    apic_nmi_fill(apic);

    /* Stop APIC */
    apic_drv->apic_read(apic_drv->vaddr, 
                        SPURIOUS_INTERRUPT_VECTOR_REGISTER, 
                        &data, 
                        1);
    

    data  &= ~APIC_SVR_ENABLE_BIT;

    apic_drv->apic_write(apic_drv->vaddr, 
                        SPURIOUS_INTERRUPT_VECTOR_REGISTER, 
                        &data, 
                        1);

    /* Set up LVT error handling */
    
    data = APIC_LVT_VECTOR_MASK(LVT_ERROR_VECTOR);

    apic_drv->apic_write(apic_drv->vaddr, 
                        LVT_ERROR_REGISTER, 
                        &data, 
                        1);

    /* Set up LINT */
    if(apic->lint == 0)
    {
        data = LVT_LINT_VECTOR                    | 
               (((uint32_t)apic->polarity) << 13) | 
               (((uint32_t)apic->trigger)  << 15);

        apic_drv->apic_write(apic_drv->vaddr, 
                            LVT_INT0_REGISTER,
                            &data, 
                            1);
    }
    else
    {
        data = LVT_LINT_VECTOR                    | 
               (((uint32_t)apic->polarity) << 13) | 
               (((uint32_t)apic->trigger)  << 15);

        apic_drv->apic_write(apic_drv->vaddr, 
                            LVT_INT1_REGISTER,
                            &data, 
                            1);
    }           

    apic_drv->apic_read(apic_drv->vaddr, 
                        SPURIOUS_INTERRUPT_VECTOR_REGISTER, 
                        &data, 
                        1);
    

    data |= APIC_SVR_VEC_MASK(SPURIOUS_VECTOR) | 
           APIC_SVR_ENABLE_BIT;

    apic_drv->apic_write(apic_drv->vaddr, 
                         SPURIOUS_INTERRUPT_VECTOR_REGISTER,
                         &data, 
                         1);


    data = 0;
    apic_drv->apic_write(apic_drv->vaddr, 
                        EOI_REGISTER, 
                        &data, 
                        1);
    if(int_status)
    {
        cpu_int_unlock();
    }
    return(0);
}

static int apic_drv_init(driver_t *drv)
{
    apic_drv_private_t  *apic_drv = NULL;

    __write_cr8(0);
    
    apic_drv = kcalloc(sizeof(apic_drv_private_t), 1);
    
    if(apic_drv == NULL)
        return(-1);

    /* Set the flag for x2APIC */

    apic_drv->x2 = apic_has_x2();

    if(apic_drv->x2)
    {      
        apic_drv->apic_read = x2apic_read;
        apic_drv->apic_write = x2apic_write;
    }
    else
    {
        apic_drv->paddr  = apic_phys_addr();

        apic_drv->vaddr  = vm_map(NULL, VM_BASE_AUTO,  
                                  PAGE_SIZE,
                                  apic_drv->paddr, 
                                  0,
                                  VM_ATTR_WRITABLE |
                                  VM_ATTR_STRONG_UNCACHED);
        
        if(apic_drv->vaddr == 0)
        {
            kfree(apic_drv);
            return(-1);
        }

        apic_drv->apic_read = xapic_read;
        apic_drv->apic_write = xapic_write;
    }

    /* Install the ISRs for the APIC */
    isr_install(apic_lvt_error_handler, 
                drv, 
                LVT_ERROR_VECTOR, 
                0, 
                &error_isr);

    isr_install(apic_spurious_handler,  
                drv, 
                SPURIOUS_VECTOR, 
                0, 
                &spur_isr);

    isr_install(apic_eoi_handler,       
                drv, 
                0,                
                1, 
                &eoi_isr);

    devmgr_drv_data_set(drv, apic_drv);

    return(0);
}


static int xapic_write
(
    virt_addr_t    reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
)
{
    uint32_t           reg_offset = 0;
    volatile uint32_t *offset     = NULL;
    uint32_t           reg_val    = 0;

    if(cnt == 0)
        return(0);

    reg_offset = (~APIC_REGISTER_START & reg) * 0x10; 

    offset = (volatile uint32_t*) (reg_offset + reg_base);

    switch(reg)
    {
        case IN_SERVICE_REGISTER:
        case TRIGGER_MODE_REGISTER:
        case INTERRUPT_REQUEST_REGISTER:
        case CURRENT_COUNT_REGISTER:
        case LOCAL_APIC_ID_REGISTER:
        case LOCAL_APIC_VERSION_REGISTER:
        case PROCESSOR_PRIORITY_REGISTER:
            return(-1);
        
        case INTERRUPT_COMMAND_REGISTER:
            if(cnt > 1)
            {
                reg_val = data[1];
                reg_val <<= 24;
                offset[4] = reg_val;
            }

        default:
            offset[0] = data[0];
    }

    return(0);
}

static int xapic_read
(
    virt_addr_t    reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
)
{
    uint32_t reg_offset = 0;
    volatile uint32_t *offset = NULL;

    if(cnt == 0)
        return(0);

    reg_offset = (~APIC_REGISTER_START & reg) * 0x10; 

    offset = (volatile uint32_t*) (reg_offset + reg_base);

    switch(reg)
    {
        case IN_SERVICE_REGISTER:
        case TRIGGER_MODE_REGISTER:
        case INTERRUPT_REQUEST_REGISTER:
        
        if(cnt > 7)
            data[7] = offset[28];
            
        if(cnt > 6)
            data[6] = offset[24];

        if(cnt > 5)
            data[5] = offset[20];

        if(cnt > 4)
            data[4] = offset[16];

        if(cnt > 3)
            data[3] = offset[12];

        if(cnt > 2)
            data[2] = offset[8];

        /* Fall through */
        case INTERRUPT_COMMAND_REGISTER:
            if(cnt > 1)
                data[1] = offset[4] >> 24;

        default:
            data[0] = offset[0];
            break;

        case EOI_REGISTER:
        case SELF_IPI_REGISTER:
            return(-1);

    }
    return(0);
}

static int x2apic_write
(
    virt_addr_t    reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
)
{
    uint64_t reg_val = 0;

    if(cnt == 0)
        return(0);
  
    switch(reg)
    {
        case IN_SERVICE_REGISTER:
        case TRIGGER_MODE_REGISTER:
        case INTERRUPT_REQUEST_REGISTER:
        case CURRENT_COUNT_REGISTER:
        case LOCAL_APIC_ID_REGISTER:
        case LOCAL_APIC_VERSION_REGISTER:
        case PROCESSOR_PRIORITY_REGISTER:
            return(-1);

        case INTERRUPT_COMMAND_REGISTER:
          
            if(cnt > 1)
            {
                reg_val = data[1];
                reg_val <<= 32;
            }

        default:
            reg_val |= data[0];

    }

    __wrmsr(reg, reg_val);

    return(0);
}

static int x2apic_read
(
    virt_addr_t    reg_base,
    uint32_t       reg,
    uint32_t      *data,
    uint32_t       cnt
)
{
    uint64_t reg_val = 0;

    if(cnt == 0)
        return(0);

    switch(reg)
    {
        case IN_SERVICE_REGISTER:
        case TRIGGER_MODE_REGISTER:
        case INTERRUPT_REQUEST_REGISTER:
        
        if(cnt > 7)
            data[7] = __rdmsr(reg + 0x70);
            
        if(cnt > 6)
            data[6] = __rdmsr(reg + 0x60);

        if(cnt > 5)
            data[5] = __rdmsr(reg + 0x50);

        if(cnt > 4)
            data[4] = __rdmsr(reg + 0x40);

        if(cnt > 3)
            data[3] = __rdmsr(reg + 0x30);

        if(cnt > 2)
            data[2] = __rdmsr(reg + 0x20);

        /* Fall through */
        case INTERRUPT_COMMAND_REGISTER:
            if(cnt > 1)
            {
                reg_val  = __rdmsr(reg);
                data[1] =  reg_val >> 32;
            }

        default:
            if(cnt > 1)
                data[0] = (uint32_t)reg_val;
            else
                data[0] = (uint32_t)__rdmsr(reg);

            break;

        case EOI_REGISTER:
        case SELF_IPI_REGISTER:
            return(-1);

    }
    return(0);
}

static intc_api_t apic_api = 
{
    .enable   = NULL,
    .disable  = NULL,
    .send_ipi = apic_send_ipi,
    .mask     = NULL,
    .unmask   = NULL
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