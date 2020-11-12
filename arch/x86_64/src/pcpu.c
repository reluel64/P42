/*
 * Platform-specific CPU routines
 */ 

#include <cpu.h>
#include <utils.h>
#include <apic.h>
#include <liballoc.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
#include <acpi.h>
#include <intc.h>
#include <devmgr.h>
#include <platform.h>
#include <timer.h>

extern void __sti();
extern void __cli();
extern int  __geti();
extern void __lidt(idt64_ptr_t *p);
extern uint64_t __read_apic_base(void);
static int cpu_on = 0;
extern void __cpu_switch_stack
(
    virt_addr_t *new_top,
    virt_addr_t *new_pos,
    virt_addr_t *old_base
);

extern void __cpuid
(
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
);



extern phys_addr_t __read_cr3(void);
extern virt_addr_t __stack_pointer(void);
extern void        halt();

extern virt_addr_t kstack_base;
extern virt_addr_t kstack_top;

extern virt_addr_t __start_ap_begin;
extern virt_addr_t __start_ap_end;
extern virt_addr_t __start_ap_pt_base;
extern virt_addr_t __start_ap_pml5_on;
extern virt_addr_t __start_ap_nx_on;
extern virt_addr_t __start_ap_stack;
extern virt_addr_t __start_ap_entry_pt;

extern virt_addr_t isr_no_ec_begin;
extern virt_addr_t isr_no_ec_end;
extern virt_addr_t isr_ec_begin;
extern virt_addr_t isr_ec_end;
extern virt_addr_t isr_ec_b;
extern virt_addr_t isr_ec_e;
extern virt_addr_t isr_no_ec_sz_start;
extern virt_addr_t isr_no_ec_sz_end;
extern virt_addr_t isr_ec_sz_start;
extern virt_addr_t isr_ec_sz_end;

static void pcpu_entry_point(void);
static spinlock_t lock;

#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)
#define _TRAMPOLINE_BEGIN ((virt_addr_t)&__start_ap_begin)
#define _TRAMPOLINE_END ((virt_addr_t)&__start_ap_end)

static int pcpu_is_bsp(void)
{
    return(!!((__read_apic_base() & (1 << 8))));
}

static uint32_t pcpu_id_get(void)
{
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    eax = 0x1F;
#if 0
    __cpuid(&eax, &ebx, &edx, &ecx);
    kprintf("EAX = 0x1F\n");

    if(eax != 0 || ebx != 0 || ecx != 0 || edx != 0)
        return(edx);
    #endif
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

static int pcpu_idt_entry_encode
(
    virt_addr_t ih,
    uint8_t type_attr,
    uint8_t ist,
    uint16_t selector,
    idt64_entry_t *idt_entry
)
{
    
    if(idt_entry == NULL)
        return(-1);

    /* set address of the handler */
    idt_entry->offset_1 = (ih & 0xffff);
    idt_entry->offset_2 = (ih & 0xffff0000) >> 16 ;
    idt_entry->offset_3 = (ih & 0xffffffff00000000) >> 32;

    /* set type, attributes and selector */
    idt_entry->seg_selector = selector;
    idt_entry->type_attr = type_attr;

    return(0);
}

static int pcpu_idt_setup(cpu_platform_drv_t *cpu_drv)
{
    idt64_entry_t      *idt = NULL;
    uint32_t            isr_size = 0;
    uint16_t            no_ec_ix = 0;
    uint16_t            ec_ix    = 0;
    virt_addr_t         ih = 0;
    
    idt = cpu_drv->idt;

    memset(idt, 0, IDT_TABLE_SIZE);
    
    /* Set up interrupt handlers */
    for(uint16_t i = 0; i < IDT_TABLE_COUNT; i++)
    {

        if(i >= RESERVED_ISR_BEGIN && i <=RESERVED_ISR_END || i == 15)
            continue;
            
        else if(i < RESERVED_ISR_BEGIN)
        {
            if((1 << i) & ISR_EC_MASK)
            {
                isr_size = (virt_addr_t)&isr_ec_sz_end - 
                           (virt_addr_t)&isr_ec_sz_start;

                ih = (virt_addr_t)&isr_ec_begin + 
                                (ec_ix * isr_size);
                ec_ix++;
            }
            else
            {
                isr_size = (virt_addr_t)&isr_no_ec_sz_end - 
                           (virt_addr_t)&isr_no_ec_sz_start;

                ih = (virt_addr_t)&isr_no_ec_begin + 
                                (no_ec_ix * isr_size);
                no_ec_ix++;
            }
        }
        else
        {
            isr_size = (virt_addr_t)&isr_no_ec_sz_end - 
                       (virt_addr_t)&isr_no_ec_sz_start;

            ih = (virt_addr_t)&isr_no_ec_begin + 
                            (no_ec_ix * isr_size);
            no_ec_ix++;
        }

        pcpu_idt_entry_encode(ih,                                   /* interrupt handler              */
                      GDT_PRESENT_SET(1) | 
                      GDT_TYPE_SET(GDT_SYSTEM_INTERUPT_GATE),  /* this is an interrupt           */
                      0,                                       /* no IST                         */
                      KERNEL_CODE_SEGMENT,                     /* isr must run in Kernel Context */
                      &idt[i]                                  /* position in the IDT            */
                     );
    }

    cpu_drv->idt_ptr.addr = (virt_addr_t)idt;
    cpu_drv->idt_ptr.limit = IDT_TABLE_SIZE - 1;
 
    return(0);
}

static void pcpu_stack_relocate
(
    virt_addr_t *new_stack_base, 
    virt_addr_t *old_stack_base
)
{
    virt_addr_t *new_stack_top = NULL;
    virt_addr_t *old_stack_top = NULL;
    virt_addr_t *sp  = NULL;
    virt_size_t stack_entries = 0;
    virt_addr_t *stack_content = NULL;
    virt_addr_t *ov = NULL;
    virt_addr_t *nv = NULL;

    /* Save the stack pointer to know
    *  how much we need to copy
    * */
    spinlock_lock_interrupt(&lock);
    sp = (virt_addr_t*)__stack_pointer();

    /* Calculate how many entries are from top to base */
    stack_entries = (old_stack_base - sp);
    new_stack_top = new_stack_base - stack_entries;
    old_stack_top = old_stack_base - stack_entries;


    kprintf("old_stack_base 0x%x old_stack_top 0x%x\n",old_stack_base, old_stack_top);
    /* Adjust stack content */

    /* Copy the content from the old stack to the new stack 
     * and do any necessary adjustments
     */

    for(virt_size_t i = 0; i < stack_entries; i++)
    {
        if((old_stack_top[i] >= (virt_addr_t)old_stack_top  ) && 
           (old_stack_top[i] <= (virt_addr_t)old_stack_base ))
           {
               /* Adjust addresses */
                new_stack_top[i] = (virt_addr_t)(new_stack_base - 
                                    (old_stack_base - (virt_addr_t*)old_stack_top[i]));

                ov = (virt_addr_t*)old_stack_top[i];
                nv = (virt_addr_t*)new_stack_top[i];

                /* adjust stack frames */
                if((ov[0] >= (virt_addr_t)old_stack_top  ) && 
                   (ov[0] <= (virt_addr_t)old_stack_base ))
                {
                    nv[0] = (virt_addr_t)(new_stack_base - 
                            (old_stack_base - (virt_addr_t*)ov[0]));
                }
           }
           else
           {
               /* Copy plain values 
                * These values contain only the values of the variables
                * of the routines
                */ 
               new_stack_top[i] = old_stack_top[i];
           }
    }

    __cpu_switch_stack(new_stack_base, 
                       new_stack_top,
                       old_stack_base
                      );

    spinlock_unlock_interrupt(&lock);
}


static virt_addr_t *pcpu_prepare_trampoline(void)
{
    virt_size_t tr_size      = 0;
    uint8_t     *tr_code     = NULL;
    phys_addr_t *pt_base     = 0;
    uint8_t     *pml5_on     = NULL;
    uint8_t     *nx_on       = NULL;
    virt_addr_t *stack       = NULL;
    virt_addr_t *entry_pt    = NULL;

    tr_size = _TRAMPOLINE_END - _TRAMPOLINE_BEGIN;

    tr_code = (uint8_t*)vmmgr_map(NULL, 
                                 CPU_TRAMPOLINE_LOCATION_START,
                                 0x0,
                                 tr_size,
                                 VMM_ATTR_EXECUTABLE |
                                 VMM_ATTR_WRITABLE);
    
    if(tr_code == NULL)
        return(NULL);
    
    /* Save some common stuff so we will place it into the 
     * relocated trampoline code
     */
    memset(tr_code, 0, tr_size);
    memcpy(tr_code, (const void*)_TRAMPOLINE_BEGIN, tr_size);

    /* Compute addresses where we will place the
     * the data for trampoline code
     */

    pml5_on  = ((virt_addr_t)&__start_ap_pml5_on                 - _TRAMPOLINE_BEGIN) + tr_code;
    nx_on    = ((virt_addr_t)&__start_ap_nx_on                   - _TRAMPOLINE_BEGIN) + tr_code;
    pt_base  = (phys_addr_t*)(((virt_addr_t)&__start_ap_pt_base  - _TRAMPOLINE_BEGIN) + tr_code);
    stack    = (virt_addr_t*)(((virt_addr_t)&__start_ap_stack    - _TRAMPOLINE_BEGIN) + tr_code);
    entry_pt = (virt_addr_t*)(((virt_addr_t)&__start_ap_entry_pt - _TRAMPOLINE_BEGIN) + tr_code);

    pml5_on[0] = pagemgr_pml5_support();
    nx_on  [0] = pagemgr_nx_support();
    pt_base[0] = __read_cr3();
    stack  [0] = (virt_addr_t)_BSP_STACK_BASE;
    entry_pt[0] = pcpu_entry_point;

    return((virt_addr_t*)tr_code);
}

int pcpu_ap_start(uint32_t cpu_start_id)
{
    int          status     = 0;
    virt_addr_t *trampoline = NULL;
    ipi_packet_t ipi;
    dev_t        *dev       = NULL;
    uint32_t     cpu_id     = 0;


    cpu_id = pcpu_id_get();
    dev = devmgr_dev_get_by_name(APIC_DRIVER_NAME, cpu_id); 

    memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);

    trampoline = pcpu_prepare_trampoline();

    if(trampoline == NULL)
    {
        return(-1);
    }

    memset(&ipi, 0, sizeof(ipi_packet_t));
#if 0
    sipi.low_bits.delivery_mode = APIC_ICR_DELIVERY_INIT;
    sipi.low_bits.dest_shortland = APIC_ICR_DEST_SHORTLAND_NO;
    sipi.low_bits.dest_mode = APIC_ICR_DEST_MODE_PHYSICAL;
    sipi.low_bits.trigger = APIC_ICR_TRIGGER_EDGE;
    sipi.low_bits.level   = 1;
    sipi.high_bits.dest_field = cpu_id;
#endif


    ipi.dest      = IPI_DEST_NO_SHORTHAND;
    ipi.dest_cpu  = cpu_start_id;
    ipi.level     = IPI_LEVEL_ASSERT;
    ipi.dest_mode = IPI_DEST_MODE_PHYS;
    ipi.trigger   = IPI_TRIGGER_EDGE;
    ipi.vector    = 0x8;
    ipi.type      = IPI_INIT;
    kprintf("CPU_START_ID %d\n",cpu_start_id);
#if 0
    sipi.low_bits.delivery_mode = APIC_ICR_DELIVERY_INIT;
    sipi.low_bits.dest_shortland = APIC_ICR_DEST_SHORTLAND_NO;
    sipi.low_bits.dest_mode = APIC_ICR_DEST_MODE_PHYSICAL;
    sipi.low_bits.trigger = APIC_ICR_TRIGGER_EDGE;
    sipi.low_bits.level   = 1;
    sipi.high_bits.dest_field = cpu_id;
#endif
 
    vmmgr_temp_identity_map(NULL, CPU_TRAMPOLINE_LOCATION_START, 
                                  CPU_TRAMPOLINE_LOCATION_START,
                                  PAGE_SIZE, 
                                  VMM_ATTR_EXECUTABLE |
                                  VMM_ATTR_WRITABLE);
                                    
    intc_send_ipi(dev, &ipi);

    ipi.type = IPI_START_AP;
 
    for(uint8_t attempt = 0; attempt < 100; attempt++)
    {
        intc_send_ipi(dev, &ipi);

        if(cpu_on)
            break;

        /* wait for about 10ms */
        timer_loop_delay(NULL, 10);
    }

    vmmgr_temp_identity_unmap(NULL, (void*)CPU_TRAMPOLINE_LOCATION_START, PAGE_SIZE);
    cpu_on = 0;
    return(0);
}

static void pcpu_entry_point(void)
{
    uint32_t cpu_id = 0;
    dev_t *dev = NULL;


       kprintf("STARTED CPU %d\n", pcpu_id_get());

    if(!devmgr_dev_create(&dev))
    {
        devmgr_dev_name_set(dev,PLATFORM_CPU_NAME);
        devmgr_dev_type_set(dev, CPU_DEVICE_TYPE);

        cpu_id = pcpu_id_get();

        devmgr_dev_index_set(dev, cpu_id);

        if(devmgr_dev_add(dev, NULL))
        {
            kprintf("FAILED TO ADD BSP CPU\n");
            return(-1);
        }
    }

 
        cpu_on = 1;

        while(1);

}


static int pcpu_setup(cpu_t *cpu)
{
    dev_t *apic_dev = NULL;
    drv_t *drv = NULL;
    cpu_platform_t *pcpu = NULL;
    cpu_platform_drv_t *pdrv = NULL;

    drv = devmgr_dev_drv_get(cpu->dev);
    pdrv = devmgr_drv_data_get(drv);

    cpu->cpu_pv = kcalloc(1, sizeof(cpu_platform_t));

    if(cpu->cpu_pv == NULL)
        return(-1);

    pcpu = cpu->cpu_pv;

    pcpu->esp0  = vmmgr_alloc(NULL, 0, PAGE_SIZE, VMM_ATTR_WRITABLE);

    /* Prepare the GDT */
    gdt_per_cpu_init(cpu->cpu_pv);

    /* Load the IDT */
    __lidt(&pdrv->idt_ptr);
    
    if(!devmgr_dev_create(&apic_dev))
    {
        devmgr_dev_name_set(apic_dev, APIC_DRIVER_NAME);
        devmgr_dev_type_set(apic_dev, INTERRUPT_CONTROLLER);
        devmgr_dev_index_set(apic_dev, cpu->cpu_id);

        if(devmgr_dev_add(apic_dev, NULL))
        {
            kprintf("%s %d failed to add device\n");
            return(-1);
           /* devmgr_dev_delete(dev); */
        }
    }

    return(0);
}

static uint32_t pcpu_get_domain
(
    uint32_t cpu_id
)
{
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_SRAT        *srat    = NULL;
    ACPI_SRAT_CPU_AFFINITY *cpu_aff = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    uint32_t                domain  = 0;

    status = AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat);

    if(ACPI_FAILURE(status))
    {
        kprintf("SRAT table not available\n");
        return(0);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_SRAT); 
        i < srat->Header.Length;
        i += subhdr->Length)
    {

        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)srat + i);

        if(subhdr->Type != ACPI_SRAT_TYPE_CPU_AFFINITY)
            continue;
        
        cpu_aff = (ACPI_SRAT_CPU_AFFINITY*)subhdr;

        if(cpu_id == cpu_aff->ApicId)
        {
            domain = 
                    (uint32_t)cpu_aff->ProximityDomainLo          | 
                    (uint32_t)cpu_aff->ProximityDomainHi[0] << 8  | 
                    (uint32_t)cpu_aff->ProximityDomainHi[1] << 16 |
                    (uint32_t)cpu_aff->ProximityDomainHi[2] << 24;
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)srat);

    return(domain);
}

static int pcpu_init(dev_t *dev)
{
    cpu_t *cpu = NULL;
    kprintf("CPU_DEV_INIT\n");
    cpu_setup(dev);

    return(0);
}

static int pcpu_probe(dev_t *dev)
{
    if(devmgr_dev_name_match(dev, PLATFORM_CPU_NAME) &&
       devmgr_dev_type_match(dev, CPU_DEVICE_TYPE))
        return(0);

    return(-1);
}

/* There's not much that 
 * can be initialized in the 
 * driver, except setting up the BSP 
 */


static int pcpu_drv_init(drv_t *drv)
{
    dev_t *cpu_bsp = NULL;
    uint32_t cpu_id = 0;
    cpu_platform_drv_t *cpu_drv = NULL;

    spinlock_init(&lock);

    cpu_drv = kcalloc(1, sizeof(cpu_platform_drv_t));

    cpu_drv->idt = vmmgr_alloc(NULL, 0x0, IDT_TABLE_SIZE, VMM_ATTR_WRITABLE);

    /* Setup the IDT */
    pcpu_idt_setup(cpu_drv);
    
    /* set up the driver's private data */
    devmgr_drv_data_set(drv, cpu_drv);
    

    if(!devmgr_dev_create(&cpu_bsp))
    {
        devmgr_dev_name_set(cpu_bsp,PLATFORM_CPU_NAME);
        devmgr_dev_type_set(cpu_bsp, CPU_DEVICE_TYPE);

        cpu_id = pcpu_id_get();

        devmgr_dev_index_set(cpu_bsp, cpu_id);

        if(devmgr_dev_add(cpu_bsp, NULL))
        {
            kprintf("FAILED TO ADD BSP CPU\n");
            return(-1);
        }
    }

    return(0);
}


static cpu_api_t cpu_api = 
{
    .cpu_setup      = pcpu_setup,
    .cpu_id_get     = pcpu_id_get,
    .cpu_get_domain = pcpu_get_domain,
    .stack_relocate = pcpu_stack_relocate,
    .int_lock       = __cli,
    .int_unlock     = __sti,
    .int_check      = __geti,
    .is_bsp         = pcpu_is_bsp
};

static drv_t x86_cpu = 
{
    .drv_name   = PLATFORM_CPU_NAME,
    .drv_type   = CPU_DEVICE_TYPE,
    .dev_probe  = pcpu_probe,
    .dev_init   = pcpu_init,
    .dev_uninit = NULL,
    .drv_init   = pcpu_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &cpu_api
};

int pcpu_register(cpu_api_t **api)
{
    (*api) = &cpu_api;
    devmgr_drv_add(&x86_cpu);
    devmgr_drv_init(&x86_cpu);
    return(0);
}
