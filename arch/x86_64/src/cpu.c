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
#include <sched.h>
#include <ioapic.h>
#include <thread.h>

#define _BSP_STACK_TOP    ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE   ((virt_addr_t)&kstack_base)
#define _TRAMPOLINE_BEGIN ((virt_addr_t)&__start_ap_begin)
#define _TRAMPOLINE_END   ((virt_addr_t)&__start_ap_end)

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

extern void *kmain_sys_init(void *arg);

static struct spinlock lock;
static volatile uint32_t cpu_on = 0;
static void cpu_ap_entry_point(void);
int32_t cpu_process_call_list
(
    void *pv,
    struct isr_info *inf
);

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
        {
            return(edx);
        }
    }

    else if(hi_leaf)
    {
        eax = 0xB;
        ebx = 0;
        ecx = 0;
        edx = 0;

        __cpuid(&eax, &ebx, &ecx, &edx);

        if(eax != 0 && ebx != 0 && ecx != 0 && edx != 0)
        {
            return(edx);
        }
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
    struct idt64_entry *idt_entry
)
{

    if(idt_entry == NULL)
    {
        return(-1);
    }
    
    /* set address of the handler */
    idt_entry->offset_1 = (ih & 0xffff);
    idt_entry->offset_2 = (ih & 0xffff0000) >> 16 ;
    idt_entry->offset_3 = (ih & 0xffffffff00000000) >> 32;

    /* set type, attributes and selector */
    idt_entry->seg_selector = selector;
    idt_entry->type_attr = type_attr;

    return(0);
}

static int cpu_idt_setup
(
    struct platform_cpu_driver *cpu_drv
)
{
    struct idt64_entry      *idt = NULL;
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
    entry_pt[0] = (virt_addr_t) cpu_ap_entry_point;
}

static int cpu_bring_ap_up
(
    struct device_node *issuer,
    uint32_t cpu,
    uint32_t timeout
)
{
    struct ipi_packet ipi;
    uint32_t expected = 0;
    struct intc_api *api = NULL;

    /* wipe the ipi garbage */
    memset(&ipi, 0, sizeof(struct ipi_packet));

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
    
    __atomic_clear(&cpu_on, __ATOMIC_SEQ_CST);

    api = devmgr_dev_api_get(issuer);

    if((api == NULL) || (api->send_ipi == NULL))
    {
        return(-1);
    }

    api->send_ipi(issuer, &ipi);

    /* Start-up SIPI */
    ipi.type = IPI_START_AP;

    sched_sleep(10);

    /* Start up the CPU */
    for(uint16_t attempt = 0; attempt < PLATFORM_AP_RETRIES; attempt++)
    {
        api->send_ipi(issuer, &ipi);

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
            
            sched_sleep(1);
        }
    }

    return(-1);
}

void cpu_signal_on
(
    void
)
{
    uint32_t current_cpu_id = 0;
    current_cpu_id = cpu_id_get();
    __atomic_store_n(&cpu_on, current_cpu_id, __ATOMIC_SEQ_CST);
}

int cpu_issue_ipi
(
    uint32_t dest,
    uint32_t cpu,
    uint32_t vector
)
{
    struct ipi_packet ipi;
    struct device_node     *dev   = NULL;
    uint32_t     cpu_id = 0;
    struct intc_api   *api   = NULL;
    int          status = -1;

    memset(&ipi, 0, sizeof(struct ipi_packet));

    switch(vector)
    {
        case IPI_SCHED:
            vector = PLATFORM_SCHED_VECTOR;
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
    api    = devmgr_dev_api_get(dev);

    if((api == NULL) || (api->send_ipi == NULL))
    {
        return(status);
    }
    
    status = api->send_ipi(dev, &ipi);

    return(status);
}

int cpu_ap_start
(
    uint32_t num,
    uint32_t timeout
)
{
    int                    status          = 0;
    virt_addr_t            trampoline      = 0;
    virt_size_t            tramp_size      = 0;
    struct device_node               *dev            = NULL;
    uint32_t               cpu_id          = 0;
    ACPI_TABLE_MADT        *madt           = NULL;
    ACPI_MADT_LOCAL_APIC   *lapic          = NULL;
    ACPI_MADT_LOCAL_X2APIC *x2lapic        = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr         = NULL;
    uint32_t               started_cpu     = 1;
    uint32_t               start_cpu_id    = 0;
    struct device_node               *target_cpu_dev = NULL;

    cpu_id = cpu_id_get();
    dev = devmgr_dev_get_by_name(APIC_DRIVER_NAME, cpu_id);

    /* Get the MADT table */
    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("MADT table not available\n");
        return(0);
    }

    if(madt == NULL)
    {
        kprintf("COULD NOT GET MADT\n");
    }

    tramp_size = ALIGN_UP( _TRAMPOLINE_END - _TRAMPOLINE_BEGIN, PAGE_SIZE);

    trampoline = vm_map(NULL,
                        CPU_TRAMPOLINE_LOCATION_START,
                        tramp_size,
                        CPU_TRAMPOLINE_LOCATION_START,
                        0,
                        VM_ATTR_EXECUTABLE |
                        VM_ATTR_WRITABLE);

    if(trampoline == VM_INVALID_ADDRESS)
    {
        AcpiPutTable((ACPI_TABLE_HEADER*)madt);
        return(-1);
    }

    /* prepare trampoline code */
    cpu_prepare_trampoline(trampoline);

    for(phys_size_t i = sizeof(ACPI_TABLE_MADT);
        (i < madt->Header.Length) && (started_cpu < num);
        i += subhdr->Length)
    {
        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)madt + i);
        start_cpu_id = -1;

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
                start_cpu_id = x2lapic->LocalApicId;
            }
        }
        else if(subhdr->Type == ACPI_MADT_TYPE_LOCAL_APIC)
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
                start_cpu_id = lapic->Id;
            }
        }

        if(start_cpu_id != -1)
        {  
            /* check if the CPU is not already started */
            target_cpu_dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, 
                                                    start_cpu_id);

            if(target_cpu_dev == NULL)
            {
                if(cpu_bring_ap_up(dev, start_cpu_id, timeout) == 0)
                {
                    started_cpu++;
                }
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    /* clear the trampoline from the area */
    memset((void*)trampoline, 0, tramp_size);

    /* Clear the stack */
    memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);

    /* unmap the trampoline */
    vm_unmap(NULL, trampoline, tramp_size);

    kprintf("Started CPUs %d\n",started_cpu);

    return(0);
}

static void cpu_ap_entry_point(void)
{
    uint32_t cpu_id = 0;
    struct device_node *timer = NULL;
    struct platform_cpu *cpu = NULL;
    uint8_t int_status = 0;

    int_status = cpu_int_check();

    if(int_status)
    {
        cpu_int_lock();
    }

    cpu = kcalloc(sizeof(struct platform_cpu), 1);
    
    cpu_id = cpu_id_get();

    /* Add cpu to the deivce manager */
       
    if(!devmgr_device_node_init(&cpu->hdr.dev))
    {
        devmgr_dev_name_set(&cpu->hdr.dev,PLATFORM_CPU_NAME);
        devmgr_dev_type_set(&cpu->hdr.dev, CPU_DEVICE_TYPE);
        devmgr_dev_index_set(&cpu->hdr.dev, cpu_id);

        if(devmgr_dev_add(&cpu->hdr.dev, NULL))
        {
            kprintf("FAILED TO ADD AP CPU\n");
        }
    }

    /* signal that the cpu is up and running */
    kprintf("CPU %d STARTED\n", cpu_id);

    /* at this point we should jump in the scheduler */
    timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, cpu_id);

    if(timer == NULL)
    {
        kprintf("NO_APIC_TIMER\n");
        timer = devmgr_dev_get_by_name(PIT8254_TIMER, 0);
    }

    /* this should not return - if it does, the loop below will halt the cpu */
    sched_unit_init(timer, &cpu->hdr, NULL);

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
        {
            continue;
        }

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


static int pcpu_dev_init
(
    struct device_node *dev
)
{
    struct cpu                 *cpu            = NULL;
    struct driver_node         *drv            = NULL;
    struct platform_cpu        *pcpu           = NULL;
    struct platform_cpu_driver *pdrv           = NULL;
    uint32_t               cpu_id         = 0;
    int                    no_apic        = 0;
    int                    status         = 0;
    uint8_t                int_status     = 0;

    int_status = cpu_int_check();

    if(int_status)
    {
        cpu_int_lock();
    }

    kprintf("INITIALIZING CPU DEVICE\n");
    
    pgmgr_per_cpu_init();
  
    drv    = devmgr_dev_drv_get(dev);
    pdrv   = (struct platform_cpu_driver*)drv;
    cpu_id = cpu_id_get();
    pcpu   = (struct platform_cpu *)dev;

    if(pcpu == NULL)
    {
        if(int_status)
        {
            cpu_int_unlock();
        }
        return(-1);
    }

    cpu = &pcpu->hdr;


    /* Store cpu id and proximity domain */
    cpu->cpu_id = cpu_id;

    /* store proximity domain of the CPU */
    cpu->proximity_domain = cpu_get_domain(cpu_id);

    /* Prepare the GDT */
    gdt_per_cpu_init(pcpu);

    /* Load the IDT */
    __lidt(&pdrv->idt_ptr);

    if(devmgr_device_node_init(&pcpu->apic.dev_node) == 0)
    {
        devmgr_dev_name_set(&pcpu->apic.dev_node, APIC_DRIVER_NAME);
        devmgr_dev_type_set(&pcpu->apic.dev_node, INTERRUPT_CONTROLLER);
        devmgr_dev_index_set(&pcpu->apic.dev_node, cpu_id);

        if(devmgr_dev_add(&pcpu->apic.dev_node, dev))
        {
            no_apic = 1;
        }
    }

    if(!no_apic)
    {
       /* Create the APIC TIMER instance of the core */
        if(devmgr_device_node_init(&pcpu->apic_tmr.dev_node) == 0)
        {
            devmgr_dev_name_set(&pcpu->apic_tmr.dev_node, APIC_TIMER_NAME);
            devmgr_dev_type_set(&pcpu->apic_tmr.dev_node, TIMER_DEVICE_TYPE);
            devmgr_dev_index_set(&pcpu->apic_tmr.dev_node, cpu_id);
    
            if(devmgr_dev_add(&pcpu->apic_tmr.dev_node, &pcpu->apic.dev_node))
            {
                devmgr_dev_delete(&pcpu->apic_tmr.dev_node);
                status = -1;
            }
        }
    }

    if(int_status)
    {
        cpu_int_unlock();
    }
    
    return(status);
}

static int pcpu_dev_probe(struct device_node *dev)
{
    if(devmgr_dev_name_match(dev, PLATFORM_CPU_NAME) &&
       devmgr_dev_type_match(dev, CPU_DEVICE_TYPE))
    {
        return(0);
    }

    return(-1);
}

/* There's not much that
 * can be initialized in the
 * driver, except setting up the BSP
 */


static int pcpu_drv_init(struct driver_node *drv)
{
    uint32_t               cpu_id    = 0;
    struct platform_cpu_driver *cpu_drv   = NULL;
    struct device_node              *timer     = NULL;
    static struct sched_thread init_th    = {0};
    uint8_t               int_status = 0;

    int_status = cpu_int_check();

    if(int_status)
    {
        cpu_int_lock();
    }

    spinlock_init(&lock);

    cpu_drv = (struct platform_cpu_driver *)drv;

    cpu_drv->idt = (struct idt64_entry *)vm_alloc(NULL, VM_BASE_AUTO,
                                                  IDT_ALLOC_SIZE,
                                                  VM_HIGH_MEM,
                                                  VM_ATTR_WRITABLE);

    if(cpu_drv->idt == (struct idt64_entry *)VM_INVALID_ADDRESS)
    {
        if(int_status)
        {
            cpu_int_unlock();
        }

        kprintf("Failed to allocate IDT\n");

        return(-1);
    }
  
    /* Setup the IDT */
    cpu_idt_setup(cpu_drv);

    /* make IDT read-only */
    vm_change_attr(NULL, (virt_addr_t)cpu_drv->idt,
                        IDT_ALLOC_SIZE,
                        0,
                        VM_ATTR_WRITABLE,
                        NULL);

    if(devmgr_device_node_init(&cpu_drv->bsp_cpu.hdr.dev) == 0)
    {
        devmgr_dev_name_set(&cpu_drv->bsp_cpu.hdr.dev,PLATFORM_CPU_NAME);
        devmgr_dev_type_set(&cpu_drv->bsp_cpu.hdr.dev, CPU_DEVICE_TYPE);

        cpu_id = cpu_id_get();

        devmgr_dev_index_set(&cpu_drv->bsp_cpu.hdr.dev, cpu_id);

        if(devmgr_dev_add(&cpu_drv->bsp_cpu.hdr.dev, NULL))
        {
            cpu_int_unlock();
            return(-1);
        }

        /* Set up the scheduler for the BSP */
        timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, cpu_id);

        if(timer == NULL)
        {
            timer = devmgr_dev_get_by_name(PIT8254_TIMER, 0);
        }
        

       /* Since we are going to jump in scheduler
        * code during sched_unit_init, we need some kind of thread
        * to continue the system initialization.
        * The entry point for this thread is going to be kmain_sys_init
        * which will carry the rest of initalization like
        * Starting the APs, detecting more HW, etc.
        */ 
       kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
       kthread_create_static(&init_th, 
                             "system_init",
                            kmain_sys_init,
                            NULL, 
                            KMAIN_SYS_INIT_STACK_SIZE, 
                            0,
                            NULL);
        isr_install(cpu_process_call_list, NULL, PLATFORM_SCHED_VECTOR, 0, &cpu_drv->ipi_isr);
        if(sched_unit_init(timer, &cpu_drv->bsp_cpu.hdr, &init_th))
        {
            if(int_status)
            {
                cpu_int_unlock();
            }

            return(-1);
        }
    }
    if(int_status)
    {
        cpu_int_unlock();
    }

    return(0);
}


static struct platform_cpu_driver x86_cpu =
{
    .drv_node.drv_name   = PLATFORM_CPU_NAME,
    .drv_node.drv_type   = CPU_DEVICE_TYPE,
    .drv_node.dev_probe  = pcpu_dev_probe,
    .drv_node.dev_init   = pcpu_dev_init,
    .drv_node.dev_uninit = NULL,
    .drv_node.drv_init   = pcpu_drv_init,
    .drv_node.drv_uninit = NULL,
    .drv_node.drv_api    = NULL,
    .idt = NULL,
    .idt_ptr = {.addr = 0, .limit = 0},
    .ipi_isr = ZERO_ISR_INIT,
    .bsp_cpu = {0}
};


int cpu_init(void)
{
    devmgr_drv_add(&x86_cpu.drv_node);
    devmgr_drv_init(&x86_cpu.drv_node);
    return(0);
}
