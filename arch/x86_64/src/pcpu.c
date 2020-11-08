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


extern void __sti();
extern void __cli();
extern int  __geti();
extern uint64_t __read_apic_base(void);

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

static spinlock_t lock = {0};

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
extern virt_addr_t __start_ap_per_cpu;


#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)
#define _TRAMPOLINE_BEGIN ((virt_addr_t)&__start_ap_begin)
#define _TRAMPOLINE_END ((virt_addr_t)&__start_ap_end)

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

#if 0
static virt_addr_t *pcpu_prepare_trampoline
(
    cpu_entry_t *cpu
)
{
    virt_size_t tr_size  = 0;
    uint8_t     *tr_code  = NULL;
    phys_addr_t *pt_base  = 0;
    uint8_t     *pml5_on = NULL;
    uint8_t     *nx_on = NULL;
    virt_addr_t *stack = NULL;
    virt_addr_t *per_cpu   = NULL;

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

    pml5_on = ((virt_addr_t)&__start_ap_pml5_on                - _TRAMPOLINE_BEGIN) + tr_code;
    nx_on   = ((virt_addr_t)&__start_ap_nx_on                  - _TRAMPOLINE_BEGIN) + tr_code;
    pt_base = (phys_addr_t*)(((virt_addr_t)&__start_ap_pt_base - _TRAMPOLINE_BEGIN) + tr_code);
    stack   = (virt_addr_t*)(((virt_addr_t)&__start_ap_stack   - _TRAMPOLINE_BEGIN) + tr_code);
    per_cpu = (virt_addr_t*)(((virt_addr_t)&__start_ap_per_cpu - _TRAMPOLINE_BEGIN) + tr_code);

    pml5_on[0] = pagemgr_pml5_support();
    nx_on  [0] = pagemgr_nx_support();
    pt_base[0] = __read_cr3();
    stack  [0] = (virt_addr_t)cpu->stack_bottom;
    per_cpu[0] = (virt_addr_t)cpu;

    return((virt_addr_t*)tr_code);
}

int pcpu_setup(cpu_entry_t *cpu)
{
    virt_addr_t old_stack_bottom = 0;
    virt_addr_t old_stack_top    = 0;

    if(cpu == NULL)
        return(-1);

    old_stack_bottom = cpu->stack_bottom;
    old_stack_top = cpu->stack_top;


    /* FIXME: Use guard pages */
    cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                             PER_CPU_STACK_SIZE,
                                             VMM_ATTR_WRITABLE);

    if(cpu->stack_top == 0)
    {
        return(-1);
    }

    /* make sure that the stacks do not contain bogus values */
    
    cpu->stack_bottom = cpu->stack_top + PER_CPU_STACK_SIZE;
    memset((void*)cpu->stack_top, 0, PER_CPU_STACK_SIZE);

    if(old_stack_bottom == 0)
    {
        pcpu_stack_relocate((virt_addr_t*)cpu->stack_bottom,
                           (virt_addr_t*)_BSP_STACK_BASE);
    }

    else
    {
        pcpu_stack_relocate((virt_addr_t*)cpu->stack_bottom,
                           (virt_addr_t*)old_stack_bottom);
    }

    /* Clear the old stacks */
    if(old_stack_top != 0)
    {
        memset((void*)old_stack_top, 0, old_stack_bottom - old_stack_top);
        vmmgr_free(NULL, (void*)old_stack_top, 
                   old_stack_bottom - old_stack_top);
    }
    else
    {
        memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);
    }

 
    pagemgr_per_cpu_init();
    pcpu_assign_domain_number(cpu);
    gdt_per_cpu_init();
    isr_per_cpu_init();
 
    return(0);
}

int cpu_ap_setup(uint32_t cpu_id)
{
    cpu_entry_t *cpu = NULL;
    virt_addr_t *trampoline = NULL;
    ipi_packet_t sipi;

    int status = 0;
    
    cpu = kmalloc(sizeof(cpu_entry_t));

    if(cpu == NULL)
        return(-1);

    memset(cpu, 0, sizeof(cpu_entry_t));
    
    cpu->cpu_id = cpu_id;

    status = cpu_add_entry(cpu);

    if(status == -1)
    {
        kprintf("Cannot add CPU %d to list\n",cpu_id);
        kfree(cpu);
        return(status);
    }
    else if(status == 1)
    {
        kprintf("CPU %d already initialized\n",cpu_id);
        kfree(cpu);
        return(status);
    }

    /* We will prepare to start the CPU 
     * and for this we will allocate a small stack
     * mainly because in case of NUMA systems, 
     * we want the stack to be allocated from the 
     * physical location that belongs to that CPU
     * so we will start the CPU and then it will relocate
     * its stack
     */
    cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                             START_AP_STACK_SIZE,
                                             VMM_ATTR_WRITABLE);

    if(cpu->stack_top == 0)
    {
        cpu_remove_entry(cpu);
        kfree(cpu);
        return(-1);
    }

    cpu->stack_bottom = cpu->stack_top + START_AP_STACK_SIZE;
    memset((void*)cpu->stack_top, 0, START_AP_STACK_SIZE);

    trampoline = cpu_prepare_trampoline(cpu);

    if(trampoline == NULL)
    {
        vmmgr_free(NULL, (void*)cpu->stack_top, START_AP_STACK_SIZE);
        cpu_remove_entry(cpu);
        kfree(cpu);
        return(-1);
    }

    memset(&sipi, 0, sizeof(apic_ipi_packet_t));
#if 0
    sipi.low_bits.delivery_mode = APIC_ICR_DELIVERY_INIT;
    sipi.low_bits.dest_shortland = APIC_ICR_DEST_SHORTLAND_NO;
    sipi.low_bits.dest_mode = APIC_ICR_DEST_MODE_PHYSICAL;
    sipi.low_bits.trigger = APIC_ICR_TRIGGER_EDGE;
    sipi.low_bits.level   = 1;
    sipi.high_bits.dest_field = cpu_id;

    vmmgr_temp_identity_map(NULL, CPU_TRAMPOLINE_LOCATION_START, 
                                  CPU_TRAMPOLINE_LOCATION_START,
                                  PAGE_SIZE, 
                                  VMM_ATTR_EXECUTABLE |
                                  VMM_ATTR_WRITABLE);

    intc_send_ipi(&sipi);

    kprintf("DELAY_DONE\n");
    memset(&sipi, 0, sizeof(apic_ipi_packet_t));
    

    sipi.low_bits.vector         = 0x8;
    sipi.low_bits.delivery_mode  = APIC_ICR_DELIVERY_START;
    sipi.low_bits.dest_shortland = APIC_ICR_DEST_SHORTLAND_NO;
    sipi.low_bits.dest_mode      = APIC_ICR_DEST_MODE_PHYSICAL;
    sipi.low_bits.trigger        = APIC_ICR_TRIGGER_EDGE;
    sipi.low_bits.level          = 1;
    sipi.high_bits.dest_field    = cpu_id;

    kprintf("sipi = %x\n",sipi.low);


    for(uint8_t attempt = 0; attempt < 10; attempt++)
    {
        intc_send_ipi(&sipi);
        apic_delay(2000000);

        if(cpu->intc)
            break;
       // for(uint32_t i = 0; i<)
    }

    vmmgr_temp_identity_unmap(NULL, (void*)CPU_TRAMPOLINE_LOCATION_START, PAGE_SIZE);

#endif
    return(0);
}

void cpu_entry_point(void)
{
    
    cpu_entry_t *cpu = NULL;
    kprintf("Started CPU %d\n",apic_id_get());

    if(cpu_get_current(&cpu) != 0)
    {
        while(1)
        {
            vga_print("HALTED\n",0x7,-1);
            halt();
        }
    }
    
    cpu_setup(cpu);
    
    while(1)
    {
        halt();
    }

}
#endif

static int pcpu_setup(cpu_t *cpu)
{
    cpu_platform_t *pcpu = NULL;

    cpu->cpu_pv = kcalloc(1, sizeof(cpu_platform_t));

    if(cpu->cpu_pv == NULL)
        return(-1);

    pcpu = cpu->cpu_pv;

    pcpu->esp0  = vmmgr_alloc(NULL, 0, PAGE_SIZE, VMM_ATTR_WRITABLE);
    cpu->cpu_id = pcpu_id_get();

    gdt_per_cpu_init(cpu->cpu_pv);
    isr_per_cpu_init();

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

static int pcpu_init(dev_t *dev)
{
    cpu_t *cpu = NULL;


    if(pcpu_is_bsp())
    {
        cpu_setup(dev);
        return(0);
    }

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
    .int_lock       = __sti,
    .int_unlock     = __cli,
    .int_check      = __geti,
    .is_bsp         = pcpu_is_bsp,
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

int pcpu_register(void)
{
    devmgr_drv_add(&x86_cpu);
    devmgr_drv_init(&x86_cpu);
    return(0);
}
