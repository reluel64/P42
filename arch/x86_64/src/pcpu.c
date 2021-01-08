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
#include <i8254.h>
#include <apic_timer.h>
#include <scheduler.h>
#include <pcpu.h>
extern void __cpu_switch_stack
(
    virt_addr_t *new_top,
    virt_addr_t *new_pos,
    virt_addr_t *old_base
);

extern void __sti();
extern void __cli();
extern int  __geti();
extern void __lidt(idt64_ptr_t *);
extern void __hlt();
extern void __pause();
extern void __cpu_context_restore(void);
extern void __resched_interrupt(void);
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

static volatile int cpu_on = 0;
static spinlock_t lock;
static void pcpu_entry_point(void);

static int pcpu_is_bsp(void)
{
    phys_addr_t apic_base = 0;

    apic_base = __rdmsr(APIC_BASE_MSR);

    return (!!(apic_base & (1 << 8)));
}

static uint32_t pcpu_id_get(void)
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

static phys_addr_t pcpu_max_phys_address(void)
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

static virt_addr_t pcpu_max_virt_address(void)
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

static int pcpu_idt_setup(cpu_platform_driver_t *cpu_drv)
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

        pcpu_idt_entry_encode(ih,                              /* interrupt handler              */
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

    sp = (virt_addr_t*)__stack_pointer();

    /* Calculate how many entries are from top to base */
    stack_entries = (old_stack_base - sp);
    new_stack_top = new_stack_base - stack_entries;
    old_stack_top = old_stack_base - stack_entries;


    kprintf("old_stack_base 0x%x old_stack_top 0x%x new_stack_base %x\n",old_stack_base, old_stack_top, new_stack_base);
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
}


static virt_addr_t pcpu_prepare_trampoline(void)
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
    
    if(tr_code == 0)
        return(0);
    
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
    entry_pt[0] =(virt_addr_t) pcpu_entry_point;

    return((virt_addr_t)tr_code);
}

static int pcpu_bring_cpu_up
(
    device_t *issuer, 
    uint32_t cpu,
    uint32_t timeout
    
)
{
    ipi_packet_t ipi;

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

    intc_send_ipi(issuer, &ipi);

    /* Start-up SIPI */
    ipi.type = IPI_START_AP;
    
    /* prepare cpu_on flag */
    __atomic_store_n(&cpu_on, 0, __ATOMIC_RELEASE);

    sched_sleep(10);

    /* Start up the CPU */
    for(uint16_t attempt = 0; attempt < timeout / 10; attempt++)
    {
        intc_send_ipi(issuer, &ipi);

        /* wait for about 1ms */
        
        for(uint32_t i = 0; i < 10;i++)
        {   
            sched_sleep(1);
           
            if(__atomic_load_n(&cpu_on, __ATOMIC_ACQUIRE))
                return(0);
        }
    }
    
    return(-1);
}

static int pcpu_issue_ipi
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

static int pcpu_ap_start
(
    uint32_t num,
    uint32_t timeout
)
{
    int                    status      = 0;
    virt_addr_t            trampoline   = 0;
    device_t               *dev     = NULL;
    uint32_t               cpu_id     = 0;
    ACPI_TABLE_MADT        *madt       = NULL;
    ACPI_MADT_LOCAL_APIC   *lapic      = NULL;
    ACPI_MADT_LOCAL_X2APIC *x2lapic    = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr     = NULL;
    int                    use_x2_apic = 0;
    uint32_t               started_cpu = 1;

    cpu_id = pcpu_id_get();
    dev = devmgr_dev_get_by_name(APIC_DRIVER_NAME, cpu_id); 

    /* Get the MADT table */
    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        kprintf("MADT table not available\n");
        return(0);
    }

    /* prepare trampoline code */
    trampoline = pcpu_prepare_trampoline();

    if(trampoline == 0)
    {
        return(-1);
    }

    /* create identity mapping for the trampoline code */
    vmmgr_temp_identity_map(NULL, CPU_TRAMPOLINE_LOCATION_START, 
                                  CPU_TRAMPOLINE_LOCATION_START,
                                  PAGE_SIZE, 
                                  VMM_ATTR_EXECUTABLE |
                                  VMM_ATTR_WRITABLE);

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
                    pcpu_bring_cpu_up(dev, x2lapic->LocalApicId, timeout);
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
                    pcpu_bring_cpu_up(dev, lapic->Id, timeout);
                    started_cpu++;
                }
            }
        }
    }

    /* unmap the 1:1 trampoline */
    vmmgr_temp_identity_unmap(NULL, CPU_TRAMPOLINE_LOCATION_START, PAGE_SIZE);

    /* clear the trampoline from the area */
    memset((void*)trampoline, 0, _TRAMPOLINE_END - _TRAMPOLINE_BEGIN);

    /* unmap the trampoline */
    vmmgr_unmap(NULL, trampoline, _TRAMPOLINE_END - _TRAMPOLINE_BEGIN);

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(0);
}

static void pcpu_entry_point(void)
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
            kprintf("FAILED TO ADD BSP CPU\n");
        }
    }   

    /* signal that the cpu is up and running */
    kprintf("CPU_STARTED\n");
    __atomic_store_n(&cpu_on, 1, __ATOMIC_RELEASE);

    cpu = devmgr_dev_data_get(cpu_dev);

    /* at this point we should jump in the scheduler */
    timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, cpu_id);

    if(timer == NULL)
        timer = devmgr_dev_get_by_name(PIT8254_TIMER, 0);

    sched_cpu_init(timer, cpu);
    
    while(1)
    {
        cpu_halt();
    }
   
}
/* Setup platform specific cpu stuff 
 * this should be called in the context of the cpu
 */ 
static int pcpu_setup(cpu_t *cpu)
{
    device_t *apic_dev          = NULL;
    device_t *apic_timer_dev    = NULL;
    driver_t *drv               = NULL;
    cpu_platform_t *pcpu        = NULL;
    cpu_platform_driver_t *pdrv = NULL;

    drv = devmgr_dev_drv_get(cpu->dev);
    pdrv = devmgr_drv_data_get(drv);

    cpu->cpu_pv = kcalloc(1, sizeof(cpu_platform_t));

    if(cpu->cpu_pv == NULL)
        return(-1);

    pcpu = cpu->cpu_pv;

    cpu->context = kcalloc(sizeof(pcpu_context_t), 1);
    
    /* Prepare the GDT */
    gdt_per_cpu_init(cpu->cpu_pv);

    /* Load the IDT */
    __lidt(&pdrv->idt_ptr);
    
    if(!devmgr_dev_create(&apic_dev))
    {
        devmgr_dev_name_set(apic_dev, APIC_DRIVER_NAME);
        devmgr_dev_type_set(apic_dev, INTERRUPT_CONTROLLER);
        devmgr_dev_index_set(apic_dev, cpu->cpu_id);

        if(devmgr_dev_add(apic_dev, cpu->dev))
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
        devmgr_dev_index_set(apic_timer_dev, cpu->cpu_id);

        if(devmgr_dev_add(apic_timer_dev, apic_dev))
        {
            kprintf("%s %d failed to add device\n",__FUNCTION__,__LINE__);
            return(-1);
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

static void pcpu_ctx_save(virt_addr_t iframe, void *th)
{
    pcpu_context_t *context = NULL;
    virt_addr_t    reg_loc = 0;
    sched_thread_t *thread = NULL;

    thread = th;
    context = thread->context;
    reg_loc = iframe - sizeof(pcpu_regs_t);
       
    memcpy(&context->iframe, (uint8_t*)iframe, sizeof(interrupt_frame_t));
    memcpy(&context->regs, (uint8_t*)reg_loc, sizeof(pcpu_regs_t));
}

static void pcpu_ctx_restore(virt_addr_t iframe, void *th)
{
    
    pcpu_context_t    *context = NULL;
    interrupt_frame_t *frame = NULL;
    cpu_t             *cpu = NULL;
    cpu_platform_t    *cpu_pv = NULL;
    sched_thread_t    *thread = NULL;

    thread = th;
    context = thread->context;
    cpu     = thread->unit->cpu;
    cpu_pv  = cpu->cpu_pv;
    
    frame = (interrupt_frame_t*)iframe;

    frame->cs = 0x8;
    frame->rsp = ((virt_addr_t)context);
    frame->rip = (uint64_t)__cpu_context_restore;
    frame->ss  = 0x10;

    /* Clear NT and IF */
    frame->rflags &= ~((1 << 14) | (1 << 9));
    
    gdt_update_tss(cpu_pv, context->esp0);
}

static void *pcpu_ctx_init
(
    void *thread,
    void *exec_pt,
    void *exec_pv
)
{
    sched_thread_t *th = NULL;
    pcpu_context_t *ctx = NULL;
    uint8_t         cs = 0x8;
    uint8_t         seg = 0x10;

    th = thread;

    ctx = (pcpu_context_t*)vmmgr_alloc(NULL, 0x0, 
                                       PAGE_SIZE, 
                                       VMM_ATTR_WRITABLE);

    if(ctx == NULL)
        return(NULL);
    
    memset(ctx, 0, PAGE_SIZE);

    ctx->iframe.rip = (virt_addr_t)exec_pt;
    ctx->iframe.rsp = th->stack + th->stack_sz;
    ctx->iframe.ss  = seg;
    /* a new task does not disable interrupts */
    ctx->iframe.rflags = 0x1 | 0x200;
    ctx->iframe.cs = cs;


    ctx->regs.rdi = (uint64_t)exec_pv;
    ctx->addr_spc = __read_cr3();
    ctx->dseg = seg;

    ctx->esp0 = vmmgr_alloc(NULL, 0, PAGE_SIZE, VMM_ATTR_WRITABLE);

    if(ctx->esp0 == 0)
    {
        vmmgr_free(NULL, (virt_addr_t)ctx, PAGE_SIZE);
        return(NULL);
    }

    return(ctx);
}

static int pcpu_ctx_destroy(void *thread)
{
    sched_thread_t *th = NULL;
    pcpu_context_t *ctx = NULL;
    
    th = thread;

    if(th == NULL)
        return(-1);

    ctx = th->context;

    if(ctx == NULL)
        return(-1);

    /* Sanitize and free ESP0 */
    memset(&ctx->esp0, 0, PAGE_SIZE);
    vmmgr_free(NULL, ctx->esp0, PAGE_SIZE);

    /* Sanitize and free context */
    memset(ctx, 0, PAGE_SIZE);
    vmmgr_free(NULL, (virt_addr_t)ctx, PAGE_SIZE);

    return(0);
}

static int pcpu_dev_init(device_t *dev)
{
    cpu_setup(dev);
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
    device_t              *pit_timer = NULL;
    cpu_t                 *cpu       = NULL;
    sched_thread_t        *init_th   = NULL;

    spinlock_init(&lock);

    cpu_drv = kcalloc(1, sizeof(cpu_platform_driver_t));

    cpu_drv->idt = (idt64_entry_t*)vmmgr_alloc(NULL, 0x0, 
                                               IDT_TABLE_SIZE, 
                                               VMM_ATTR_WRITABLE);

    /* Setup the IDT */
    pcpu_idt_setup(cpu_drv);
    
    /* make IDT read-only */
    vmmgr_change_attrib(NULL, (virt_addr_t)cpu_drv->idt, 
                        IDT_TABLE_SIZE, 
                        ~VMM_ATTR_WRITABLE);

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
        else
        {
            cpu = devmgr_dev_data_get(cpu_bsp);
            timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, cpu_id);
            
            if(timer == NULL)
                timer = devmgr_dev_get_by_name(PIT8254_TIMER, 0);

            if(sched_cpu_init(timer, cpu))
            {
                return(-1);
            }
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
    .is_bsp         = pcpu_is_bsp,
    .start_ap       = pcpu_ap_start,
    .max_virt_addr  = pcpu_max_virt_address,
    .max_phys_addr  = pcpu_max_phys_address,
    .ipi_issue      = pcpu_issue_ipi,
    .halt           = __hlt,
    .pause          = __pause,
    .ctx_save       = pcpu_ctx_save,
    .ctx_restore    = pcpu_ctx_restore,
    .ctx_init       = pcpu_ctx_init,
    .ctx_destroy    = pcpu_ctx_destroy,
    .resched        = __resched_interrupt
};

static driver_t x86_cpu = 
{
    .drv_name   = PLATFORM_CPU_NAME,
    .drv_type   = CPU_DEVICE_TYPE,
    .dev_probe  = pcpu_dev_probe,
    .dev_init   = pcpu_dev_init,
    .dev_uninit = NULL,
    .drv_init   = pcpu_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &cpu_api
};

int pcpu_init(void)
{
    devmgr_drv_add(&x86_cpu);
    devmgr_drv_init(&x86_cpu);
    return(0);
}

int pcpu_api_register(cpu_api_t **api)
{
    *api = &cpu_api;
    return(0);
}