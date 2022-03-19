/*
 * Platform-specific CPU routines
 */

#include <cpu.h>
#include <utils.h>
#include <apic.h>
#include <liballoc.h>
#include <vm.h>
#include <gdt.h>
#include <isr.h>
#include <acpi.h>
#include <intc.h>
#include <devmgr.h>
#include <platform.h>
#include <timer.h>
#include <i8254.h>
#include <apic_timer.h>
#include <scheduler.h>
#include <ioapic.h>

extern void __cpu_switch_stack
(
    virt_addr_t *new_top,
    virt_addr_t *new_pos,
    virt_addr_t *old_base
);




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



#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)
#define _TRAMPOLINE_BEGIN ((virt_addr_t)&__start_ap_begin)
#define _TRAMPOLINE_END ((virt_addr_t)&__start_ap_end)

static spinlock_t lock;
static volatile int cpu_on = 0;
static void cpu_entry_point(void);


static int pcpu_is_bsp(void)
{
    phys_addr_t apic_base = 0;

    apic_base = __rdmsr(APIC_BASE_MSR);

    return (!!(apic_base & (1 << 8)));
}

uint32_t cpu_id_get(void)
{
    static uint32_t hi_leaf = 0;
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    
    if(!hi_leaf)
    {
        eax = 0x0;

        __cpuid(&eax, &ebx, &ecx, &edx);

        hi_leaf = eax;
    }

    if(hi_leaf >= 0x1F)
    {
        eax = 0x1F;
        ebx = 0;
        ecx = 0;
        edx = 0;

        __cpuid(&eax, &ebx, &ecx, &edx);

        if(eax != 0 && ebx != 0 && ecx != 0 && edx != 0)
            return(edx);
    }

    else if(hi_leaf)
    {
        eax = 0xB;
        ebx = 0;
        ecx = 0;
        edx = 0;

        __cpuid(&eax, &ebx, &ecx, &edx);

        if(eax != 0 && ebx != 0 && ecx != 0 && edx != 0)
            return(edx);
    }

    eax = 0x1;
    ecx = 0;

    __cpuid(&eax, &ebx, &ecx, &edx);

    return((ebx >> 24) & 0xFF);
}

phys_addr_t cpu_phys_max(void)
{
    uint32_t    eax       = 0;
    uint32_t    ebx       = 0;
    uint32_t    ecx       = 0;
    uint32_t    edx       = 0;
    phys_addr_t phys_addr = 1;

    eax = 0x80000008;

    __cpuid(&eax, &ebx, &ecx, &edx);

    /* clear other stuff */
    eax = eax & 0xFF;

    /* compute the maximum physical address */
    phys_addr = phys_addr << eax;

    return(phys_addr - 1);
}

virt_addr_t cpu_virt_max(void)
{
    uint32_t    eax       = 0;
    uint32_t    ebx       = 0;
    uint32_t    ecx       = 0;
    uint32_t    edx       = 0;
    virt_addr_t virt_addr = 1;

    eax = 0x80000008;

    __cpuid(&eax, &ebx, &ecx, &edx);

    /* clear other stuff */
    eax = (eax >> 8) & 0xFF;

    /* compute the maximum physical address */
    virt_addr = virt_addr << eax;

    return(virt_addr - 1);
}


static int cpu_idt_entry_encode
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

static int cpu_idt_setup(cpu_platform_driver_t *cpu_drv)
{
    idt64_entry_t      *idt = NULL;
    uint32_t            isr_size = 0;
    uint16_t            no_ec_ix = 0;
    uint16_t            ec_ix    = 0;
    virt_addr_t         ih = 0;

    idt = cpu_drv->idt;

    memset(idt, 0, IDT_TABLE_SIZE);
    kprintf("IDT %x\n", idt);
    /* Set up interrupt handlers */
    for(uint16_t i = 0; i < IDT_TABLE_COUNT; i++)
    {

        if((i >= RESERVED_ISR_BEGIN && i <=RESERVED_ISR_END) || i == 15)
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

        cpu_idt_entry_encode(ih,                              /* interrupt handler              */
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

static void cpu_prepare_trampoline
(
    virt_addr_t low_trampoline
)
{
    virt_size_t tr_size      = 0;
    uint8_t     *tr_code     = NULL;
    phys_addr_t *pt_base     = 0;
    uint8_t     *pml5_on     = NULL;
    uint8_t     *nx_on       = NULL;
    virt_addr_t *stack       = NULL;
    virt_addr_t *entry_pt    = NULL;

    tr_size = ALIGN_UP(_TRAMPOLINE_END - _TRAMPOLINE_BEGIN, PAGE_SIZE);
    tr_code = (uint8_t*)low_trampoline;

    /* Save some common stuff so we will place it into the
     * relocated trampoline code
     */
    memset(tr_code, 0, tr_size);
    memcpy(tr_code, (const void*)_TRAMPOLINE_BEGIN, tr_size);

    /* Compute addresses where we will place the
     * the data for trampoline code
     */

    pml5_on  = ((virt_addr_t)&__start_ap_pml5_on                 
                             - _TRAMPOLINE_BEGIN) + tr_code;

    nx_on    = ((virt_addr_t)&__start_ap_nx_on   
                             - _TRAMPOLINE_BEGIN) + tr_code;

    pt_base  = (phys_addr_t*)(((virt_addr_t)&__start_ap_pt_base  
                                            - _TRAMPOLINE_BEGIN) + tr_code);

    stack    = (virt_addr_t*)(((virt_addr_t)&__start_ap_stack    
                                            - _TRAMPOLINE_BEGIN) + tr_code);

    entry_pt = (virt_addr_t*)(((virt_addr_t)&__start_ap_entry_pt 
                                            - _TRAMPOLINE_BEGIN) + tr_code);

    pml5_on [0] = pgmgr_pml5_support();
    nx_on   [0] = pgmgr_nx_support();
    pt_base [0] = __read_cr3();
    stack   [0] = (virt_addr_t)_BSP_STACK_BASE;
    entry_pt[0] = (virt_addr_t) cpu_entry_point;
}

static int cpu_bring_cpu_up
(
    device_t *issuer,
    uint32_t cpu,
    uint32_t timeout
)
{
    ipi_packet_t ipi;
    uint32_t expected = 0;
    /* wipe the ipi garbage */
    memset(&ipi, 0, sizeof(ipi_packet_t));

    /* wipe the temporary stack */
    memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);

    /* set up some common stuff */
    ipi.dest      = IPI_DEST_NO_SHORTHAND;
    ipi.level     = IPI_LEVEL_ASSERT;
    ipi.dest_mode = IPI_DEST_MODE_PHYS;
    ipi.trigger   = IPI_TRIGGER_EDGE;
    ipi.vector    = 0x8;

    /* INIT IPI */
    ipi.type      = IPI_INIT;
    ipi.dest_cpu  = cpu;
    
    /* prepare cpu_on flag */
    kprintf("CPU_FLAG_PRE %d\n", cpu_on);
    __atomic_and_fetch(&cpu_on, 0, __ATOMIC_SEQ_CST);
    kprintf("CPU_FLAG_POST %d\n", cpu_on);
    intc_send_ipi(issuer, &ipi);
    
    /* Start-up SIPI */
    ipi.type = IPI_START_AP;

    sched_sleep(10);
   

    /* Start up the CPU */
    for(uint16_t attempt = 0; attempt < 10; attempt++)
    {
       
        intc_send_ipi(issuer, &ipi);

        /* wait for about 10ms */
 
        for(uint32_t i = 0; i < timeout ;i++)
        {
            expected = cpu;
            if(__atomic_compare_exchange_n(&cpu_on, &expected, 0, 0,
                __ATOMIC_SEQ_CST, 
                __ATOMIC_SEQ_CST ))
            {
                return(0);
            }
            
            sched_sleep(10);
        }
    }
    return(-1);
}

void cpu_signal_on(uint32_t id)
{
    __atomic_or_fetch(&cpu_on, id, __ATOMIC_SEQ_CST);
}

int cpu_issue_ipi
(
    uint8_t dest,
    uint32_t cpu,
    uint32_t vector
)
{
    ipi_packet_t ipi;
    device_t *dev   = NULL;
    uint32_t cpu_id = 0;

    memset(&ipi, 0, sizeof(ipi_packet_t));

    switch(vector)
    {
        case IPI_RESCHED:
            vector = PLATFORM_RESCHED_VECTOR;
            break;

        case IPI_INVLPG:
            vector = PLATFORM_PG_INVALIDATE_VECTOR;
            break;
    }

    ipi.dest      = dest;
    ipi.level     = IPI_LEVEL_ASSERT;
    ipi.dest_mode = IPI_DEST_MODE_PHYS;
    ipi.trigger   = IPI_TRIGGER_EDGE;
    ipi.vector    = vector;
    ipi.dest_cpu  = cpu;

    cpu_id = cpu_id_get();
    dev    = devmgr_dev_get_by_name(APIC_DRIVER_NAME, cpu_id);

    intc_send_ipi(dev, &ipi);

    return(0);
}

int cpu_ap_start
(
    uint32_t num,
    uint32_t timeout
)
{
    int                    status      = 0;
    virt_addr_t            trampoline  = 0;
    virt_size_t            tramp_size  = 0;
    device_t               *dev        = NULL;
    uint32_t               cpu_id      = 0;
    ACPI_TABLE_MADT        *madt       = NULL;
    ACPI_MADT_LOCAL_APIC   *lapic      = NULL;
    ACPI_MADT_LOCAL_X2APIC *x2lapic    = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr     = NULL;
    int                    use_x2_apic = 0;
    uint32_t               started_cpu = 1;

    cpu_id = cpu_id_get();
    dev = devmgr_dev_get_by_name(APIC_DRIVER_NAME, cpu_id);

    /* Get the MADT table */
    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("MADT table not available\n");
        return(0);
    }

    tramp_size = ALIGN_UP( _TRAMPOLINE_END - _TRAMPOLINE_BEGIN, PAGE_SIZE);

    trampoline = vm_map(NULL,CPU_TRAMPOLINE_LOCATION_START,
                tramp_size,
                CPU_TRAMPOLINE_LOCATION_START,
                0,
                VM_ATTR_EXECUTABLE |
                VM_ATTR_WRITABLE);

    if(trampoline == VM_INVALID_ADDRESS)
    {
        return(-1);
    }

    /* prepare trampoline code */
    cpu_prepare_trampoline(trampoline);

    /* check if are going to use x2APIC */
    for(phys_size_t i = sizeof(ACPI_TABLE_MADT);
        i < madt->Header.Length;
        i += subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
        {
            use_x2_apic = 1;
            break;
        }
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT);
        (i < madt->Header.Length) && (started_cpu < num);
        i += subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);

        if(use_x2_apic)
        {
            if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_X2APIC)
            {
                x2lapic = (ACPI_MADT_LOCAL_X2APIC*)subhdr;

                if(cpu_id == x2lapic->LocalApicId)
                {
                    continue;
                }
                else if(((x2lapic->LapicFlags & 0x1) == 0) &&
                        ((x2lapic->LapicFlags & 0x2) == 0))
                {
                    continue;
                }
                else
                {
                    if(cpu_bring_cpu_up(dev, x2lapic->LocalApicId, timeout) == 0)
                        started_cpu++;
                }
            }
        }
        else
        {
            if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_APIC)
            {
                lapic = (ACPI_MADT_LOCAL_APIC*)subhdr;

                if(cpu_id == lapic->Id)
                {
                    continue;
                }
                else if(((lapic->LapicFlags & 0x1) == 0) &&
                        ((lapic->LapicFlags & 0x2) == 0))
                {
                    continue;
                }
                else
                {
                    if(cpu_bring_cpu_up(dev, lapic->Id, timeout) == 0)
                        started_cpu++;
                }
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    /* unmap the 1:1 trampoline */
    /*(NULL, CPU_TRAMPOLINE_LOCATION_START, PAGE_SIZE);*/

    /* clear the trampoline from the area */
    memset((void*)trampoline, 0, tramp_size);

    /* Clear the stack */
     memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);

    /* unmap the trampoline */
    vm_unmap(NULL, trampoline, tramp_size);

    kprintf("Started CPUs %d\n",started_cpu);

    return(0);
}

static void cpu_entry_point(void)
{
    uint32_t cpu_id = 0;
    device_t *timer = NULL;
    device_t *cpu_dev = NULL;
    cpu_t *cpu = NULL;
    cpu_id = cpu_id_get();

    /* Add cpu to the deivce manager */
       
    if(!devmgr_dev_create(&cpu_dev))
    {
        devmgr_dev_name_set(cpu_dev,PLATFORM_CPU_NAME);
        devmgr_dev_type_set(cpu_dev, CPU_DEVICE_TYPE);
        devmgr_dev_index_set(cpu_dev, cpu_id);

        if(devmgr_dev_add(cpu_dev, NULL))
        {
            kprintf("FAILED TO ADD AP CPU\n");
        }
    }

    /* signal that the cpu is up and running */
    kprintf("CPU %d STARTED\n", cpu_id);
    

    cpu = devmgr_dev_data_get(cpu_dev);

    /* at this point we should jump in the scheduler */
    timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, cpu_id);

    if(timer == NULL)
    {
        kprintf("NO_APIC_TIMER\n");
        timer = devmgr_dev_get_by_name(PIT8254_TIMER, 0);
    }

    sched_unit_init(timer, cpu);

    kprintf("HALTING CPU %x\n",cpu_id);

    while(1)
    {
        cpu_halt();
    }

}

static uint32_t cpu_get_domain
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


static int pcpu_dev_init(device_t *dev)
{
    cpu_t                 *cpu            = NULL;
    device_t              *apic_dev       = NULL;
    device_t              *apic_timer_dev = NULL;
    driver_t              *drv            = NULL;
    cpu_platform_t        *pcpu           = NULL;
    cpu_platform_driver_t *pdrv           = NULL;
    uint32_t               cpu_id         = 0;
    kprintf("INITIALIZING CPU DEVICE\n");
    cpu_int_lock();
    
    pgmgr_per_cpu_init();
  
    drv    = devmgr_dev_drv_get(dev);
    pdrv   = devmgr_drv_data_get(drv);
    cpu_id = cpu_id_get();

    cpu = kcalloc(sizeof(cpu_t), 1);

    if(cpu == NULL)
        return(-1);

    pcpu = kcalloc(sizeof(cpu_platform_t), 1);
    
    if(pcpu == NULL)
    {
        kfree(cpu);
        return(-1);
    }

    cpu->dev = dev;

    devmgr_dev_data_set(dev, cpu);

    cpu->cpu_id = cpu_id;
    cpu->proximity_domain = cpu_get_domain(cpu_id);

    cpu->cpu_pv = pcpu;

    /* Prepare the GDT */
    gdt_per_cpu_init(cpu->cpu_pv);

    /* Load the IDT */
    __lidt(&pdrv->idt_ptr);
      
    if(!devmgr_dev_create(&apic_dev))
    {
        devmgr_dev_name_set(apic_dev, APIC_DRIVER_NAME);
        devmgr_dev_type_set(apic_dev, INTERRUPT_CONTROLLER);
        devmgr_dev_index_set(apic_dev, cpu_id);

        if(devmgr_dev_add(apic_dev, dev))
        {
            kprintf("%s %d failed to add device\n",__FUNCTION__,__LINE__);
            return(-1);
        }

        kprintf("DEV_TYPE %s\n",devmgr_dev_type_get(apic_dev));
    }

    if(!devmgr_dev_create(&apic_timer_dev))
    {
        devmgr_dev_name_set(apic_timer_dev, APIC_TIMER_NAME);
        devmgr_dev_type_set(apic_timer_dev, TIMER_DEVICE_TYPE);
        devmgr_dev_index_set(apic_timer_dev, cpu_id);

        if(devmgr_dev_add(apic_timer_dev, apic_dev))
        {
            kprintf("%s %d failed to add device\n",__FUNCTION__,__LINE__);
            return(-1);
        }
    }

    return(0);
}

static int pcpu_dev_probe(device_t *dev)
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


static int pcpu_drv_init(driver_t *drv)
{
    device_t              *cpu_bsp   = NULL;
    uint32_t               cpu_id    = 0;
    cpu_platform_driver_t *cpu_drv   = NULL;
    device_t              *timer     = NULL;
    cpu_t                 *cpu       = NULL;
    sched_thread_t        *init_th   = NULL;

    spinlock_init(&lock);

    cpu_drv = kcalloc(1, sizeof(cpu_platform_driver_t));

    cpu_drv->idt = (idt64_entry_t*)vm_alloc(NULL, VM_BASE_AUTO,
                                                  IDT_ALLOC_SIZE,
                                                  VM_HIGH_MEM,
                                                  VM_ATTR_WRITABLE);

    if(cpu_drv->idt == (idt64_entry_t*)VM_INVALID_ADDRESS)
    {
        kprintf("Failed to allocate IDT\n");
        while(1);
    }
  
    /* Setup the IDT */
    cpu_idt_setup(cpu_drv);

    /* make IDT read-only */
    vm_change_attr(NULL, (virt_addr_t)cpu_drv->idt,
                        IDT_ALLOC_SIZE,
                        0,
                        VM_ATTR_WRITABLE,
                        NULL);
 
    /* set up the driver's private data */
    devmgr_drv_data_set(drv, cpu_drv);

    if(!devmgr_dev_create(&cpu_bsp))
    {
        devmgr_dev_name_set(cpu_bsp,PLATFORM_CPU_NAME);
        devmgr_dev_type_set(cpu_bsp, CPU_DEVICE_TYPE);

        cpu_id = cpu_id_get();

        devmgr_dev_index_set(cpu_bsp, cpu_id);

        if(devmgr_dev_add(cpu_bsp, NULL))
        {
            kprintf("FAILED TO ADD BSP CPU\n");
            return(-1);
        }

        /* Set up the scheduler for the BSP */
        cpu = devmgr_dev_data_get(cpu_bsp);
        timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, cpu_id);

        if(timer == NULL)
        {
            timer = devmgr_dev_get_by_name(PIT8254_TIMER, 0);
        }

        if(sched_unit_init(timer, cpu))
        {
            return(-1);
        }
    }

    return(0);
}

cpu_t *cpu_current_get(void)
{
    device_t *dev = NULL;
    uint32_t cpu_id = 0;
    cpu_t    *cpu = NULL;
    int int_state = 0;

    int_state = cpu_int_check();

    if(int_state)
        cpu_int_lock();

    cpu_id = cpu_id_get();
    
    dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, cpu_id);
    cpu = devmgr_dev_data_get(dev);

    if(int_state)
        cpu_int_unlock();

    return(cpu);
}

static driver_t x86_cpu =
{
    .drv_name   = PLATFORM_CPU_NAME,
    .drv_type   = CPU_DEVICE_TYPE,
    .dev_probe  = pcpu_dev_probe,
    .dev_init   = pcpu_dev_init,
    .dev_uninit = NULL,
    .drv_init   = pcpu_drv_init,
    .drv_uninit = NULL,
    .drv_api    = NULL
};

int cpu_init(void)
{
    devmgr_drv_add(&x86_cpu);
    devmgr_drv_init(&x86_cpu);
    return(0);
}
