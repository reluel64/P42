#include <cpu.h>
#include <types.h>
#include <utils.h>
#include <apic.h>
#include <liballoc.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
#include <acpi.h>

#define CPU_TRAMPOLINE_LOCATION_START (0x8000)
#define PER_CPU_STACK_SIZE            (0x8000) /* 32 K */
#define START_AP_STACK_SIZE           (PAGE_SIZE) /* 4K */

static list_head_t cpu_list;
static spinlock_t lock;

extern void __cpu_switch_stack
(
    virt_addr_t *new_top,
    virt_addr_t *new_pos,
    virt_addr_t *old_base
);

static int cpu_assign_domain_number
(
    cpu_entry_t *cpu
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
extern virt_addr_t __start_ap_per_cpu;


#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)
#define _TRAMPOLINE_BEGIN ((virt_addr_t)&__start_ap_begin)
#define _TRAMPOLINE_END ((virt_addr_t)&__start_ap_end)

int cpu_add_entry
(
    cpu_entry_t *cpu_in
)
{
    cpu_entry_t *cpu  = NULL;
    cpu_entry_t *next_cpu = NULL;
    list_head_t *head = NULL;

    spinlock_lock_interrupt(&lock);
    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->cpu_id == cpu_in->cpu_id)
        {
            spinlock_unlock_interrupt(&lock);
            return(1);
        }
        cpu = next_cpu;
    }

    linked_list_add_tail(head, &cpu_in->node);
    spinlock_unlock_interrupt(&lock);
    return(0);
}

int cpu_remove_entry
(
    cpu_entry_t *cpu_in
)
{
    cpu_entry_t *cpu  = NULL;
    cpu_entry_t *next_cpu = NULL;
    list_head_t *head = NULL;

    spinlock_lock_interrupt(&lock);
    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->cpu_id == cpu_in->cpu_id)
        {
            linked_list_remove(head, &cpu->node);
            spinlock_unlock_interrupt(&lock);
            return(0);
        }
        cpu = next_cpu;
    }
    spinlock_unlock_interrupt(&lock);
    return(-1);
}

int cpu_get_entry
(
    uint32_t apic_id, 
    cpu_entry_t **cpu_out
)
{
    cpu_entry_t *cpu  = NULL;
    cpu_entry_t *next_cpu = NULL;
    list_head_t *head = NULL;

    spinlock_lock_interrupt(&lock);

    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->cpu_id == apic_id)
        {
            *cpu_out = cpu;
            spinlock_unlock_interrupt(&lock);
            return(0);
        }
        cpu = next_cpu;
    }
    spinlock_unlock_interrupt(&lock);
    return(-1);
}

static void cpu_stack_relocate
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


static virt_addr_t *cpu_prepare_trampoline
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

    pml5_on = ((virt_addr_t)&__start_ap_pml5_on - _TRAMPOLINE_BEGIN) + tr_code;
    nx_on   = ((virt_addr_t)&__start_ap_nx_on   - _TRAMPOLINE_BEGIN) + tr_code;
    pt_base = (phys_addr_t*)(((virt_addr_t)&__start_ap_pt_base - _TRAMPOLINE_BEGIN) + tr_code);
    stack   = (virt_addr_t*)(((virt_addr_t)&__start_ap_stack   - _TRAMPOLINE_BEGIN) + tr_code);
    per_cpu = (virt_addr_t*)(((virt_addr_t)&__start_ap_per_cpu - _TRAMPOLINE_BEGIN) + tr_code);

    pml5_on[0] = pagemgr_pml5_support();
    nx_on  [0] = pagemgr_nx_support();
    pt_base[0] = __read_cr3();
    stack  [0] = (virt_addr_t)cpu->stack_bottom;
    per_cpu[0] = (virt_addr_t)cpu;
}

static int cpu_bsp_setup(void)
{
    uint32_t cpu_id = 0;
    cpu_entry_t *cpu = NULL;
    int status = 0;

    if(!apic_is_bsp())
        return(-1);

    cpu_id = apic_id_get();

    cpu = kmalloc(sizeof(cpu_entry_t));

    if(cpu == NULL)
        return(-1);

    memset(cpu, 0, sizeof(cpu_entry_t));

    cpu->cpu_id = cpu_id;

    status = cpu_add_entry(cpu);

    if(status != 0)
        return(-1);

    /* TODO: USE GUARD PAGES */
    
    cpu_assign_domain_number(cpu);
    cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                             PER_CPU_STACK_SIZE,
                                             VMM_ATTR_WRITABLE  | 
                                             VMM_GUARD_MEMORY);

    if(cpu->stack_top == 0)
    {
        return(-1);
    }

    cpu->stack_bottom = cpu->stack_top + PER_CPU_STACK_SIZE;
    memset((void*)cpu->stack_top, 0, PER_CPU_STACK_SIZE);

    cpu_stack_relocate((virt_addr_t*)cpu->stack_bottom,
                       (virt_addr_t*)_BSP_STACK_BASE);

    
    apic_cpu_init(cpu);
    
    
    return(0);
}

int cpu_get_current(cpu_entry_t **cpu_out)
{
    uint32_t cpu_id = 0;

    cpu_id = apic_id_get();

    return(cpu_get_entry(cpu_id, cpu_out));
}

int cpu_ap_setup(uint32_t cpu_id)
{
    cpu_entry_t *cpu = NULL;
    cpu_entry_t *invoker_cpu = NULL;
    virt_addr_t *trampoline = NULL;
    apic_ipi_packet_t sipi;

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

    
    sipi.low_bits.delivery_mode = APIC_ICR_DELIVERY_INIT;
    sipi.low_bits.dest_shortland = APIC_ICR_DEST_SHORTLAND_NO;
    sipi.low_bits.dest_mode = APIC_ICR_DEST_MODE_PHYSICAL;
    sipi.low_bits.trigger = APIC_ICR_TRIGGER_EDGE;
    sipi.low_bits.level   = 1;
    sipi.high_bits.dest_field = cpu_id;

    invoker_cpu = (cpu_entry_t*)linked_list_first(&cpu_list);


    vmmgr_temp_identity_map(NULL, CPU_TRAMPOLINE_LOCATION_START, 
                                  CPU_TRAMPOLINE_LOCATION_START,
                                  PAGE_SIZE, 
                                  VMM_ATTR_EXECUTABLE |
                                  VMM_ATTR_WRITABLE);


    apic_send_ipi(invoker_cpu, &sipi);

    for(uint64_t i= 0; i<1000000;i++);
    vga_print("DELAY_DONE\n",0x7,-1);
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
    vga_print("WAITING\n",0x7,-1);

    


    for(uint8_t attempt = 0; attempt < 10; attempt++)
    {
        apic_send_ipi(invoker_cpu, &sipi);
        for(uint64_t i = 0; i < 0x100000ull;i++);

        if(cpu->intc)
            break;
       // for(uint32_t i = 0; i<)
    }
    vga_print("WAIT_DONE\n",0x7,-1);
    vmmgr_temp_identity_unmap(NULL, (void*)CPU_TRAMPOLINE_LOCATION_START, PAGE_SIZE);

    return(0);
}

void cpu_entry_point()
{
    
    cpu_entry_t *cpu = NULL;
    virt_addr_t old_stack_bottom = 0;
    virt_addr_t old_stack_top = 0;
    kprintf("Started CPU %d\n",apic_id_get());

    if(cpu_get_current(&cpu) != 0)
    {
        while(1)
        {
            vga_print("HALTED\n",0x7,-1);
            halt();
        }
    }
    
    pagemgr_per_cpu_init();
    cpu_assign_domain_number(cpu);
    gdt_per_cpu_init();
    isr_per_cpu_init();
    apic_cpu_init(cpu);
    

    old_stack_bottom = cpu->stack_bottom;
    old_stack_top = cpu->stack_top;

    cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL,
                                              0x0, 
                                              PER_CPU_STACK_SIZE, 
                                              VMM_ATTR_WRITABLE);

    cpu->stack_bottom = cpu->stack_top + PER_CPU_STACK_SIZE;
    kprintf("CPU %d @ %d\n",cpu->cpu_id,cpu->proximity_domain);
    if(cpu->stack_top == 0)
    {
        while(1)
            halt();
    }
    
    cpu_stack_relocate((virt_addr_t*)cpu->stack_bottom, 
                       (virt_addr_t*)old_stack_bottom);

    vmmgr_free(NULL,(void*)old_stack_top, START_AP_STACK_SIZE);



    while(1)
    {
        halt();
    }


while(1);
}

int cpu_init(void)
{
    linked_list_init(&cpu_list);
    spinlock_init(&lock);
    
    gdt_per_cpu_init();

    if(isr_init()!= 0)
        return;

    return(cpu_bsp_setup());
}


static int cpu_assign_domain_number
(
    cpu_entry_t *cpu
)
{
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_SRAT        *srat    = NULL;
    ACPI_SRAT_CPU_AFFINITY *cpu_aff = NULL;
    ACPI_SUBTABLE_HEADER   *subhdr  = NULL;
    
    status = AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat);

    if(ACPI_FAILURE(status))
    {
        kprintf("SRAT table not available\n");
        return(-1);
    }

    for(phys_size_t i = sizeof(ACPI_TABLE_SRAT); 
        i < srat->Header.Length;
        i += subhdr->Length)
    {

        subhdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)srat + i);

        if(subhdr->Type != ACPI_SRAT_TYPE_CPU_AFFINITY)
            continue;
        
        cpu_aff = (ACPI_SRAT_CPU_AFFINITY*)subhdr;

        if(cpu->cpu_id == cpu_aff->ApicId)
        {
            cpu->proximity_domain = 
                      (uint32_t)cpu_aff->ProximityDomainLo          | 
                      (uint32_t)cpu_aff->ProximityDomainHi[0] << 8  | 
                      (uint32_t)cpu_aff->ProximityDomainHi[1] << 16 |
                      (uint32_t)cpu_aff->ProximityDomainHi[2] << 24;
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)srat);

    return(0);
}
