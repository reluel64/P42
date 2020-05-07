#include <cpu.h>
#include <types.h>
#include <utils.h>
#include <apic.h>
#include <liballoc.h>
#include <vmmgr.h>

#define CPU_TRAMPOLINE_LOCATION_START 0x8000


static list_head_t cpu_list = {.list.next = NULL, 
                               .list.prev = NULL,
                               .count = 0};

extern void __cpu_switch_stack
(
    virt_addr_t *new_top,
    virt_addr_t *new_pos,
    virt_addr_t *old_base
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



#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)
#define _TRAMPOLINE_BEGIN ((virt_addr_t)&__start_ap_begin)
#define _TRAMPOLINE_END ((virt_addr_t)&__start_ap_end)

int cpu_add_entry
(
    uint32_t apic_id, 
    cpu_entry_t **cpu_out
)
{
    cpu_entry_t *cpu  = NULL;
    cpu_entry_t *next_cpu = NULL;
    list_head_t *head = NULL;


    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->apic_id == apic_id)
        {
            *cpu_out = cpu;
            return(1);
        }
        cpu = next_cpu;
    }

    cpu = kmalloc(sizeof(cpu_entry_t));

    if(cpu == NULL)
        return(-1);

    memset(cpu, 0, sizeof(cpu_entry_t));

    linked_list_add_tail(head, &cpu->node);
    
    cpu->apic_id = apic_id;
    *cpu_out = cpu; 
    return(0);
}

int cpu_remove_entry
(
    uint32_t apic_id, 
    cpu_entry_t **cpu_out
)
{
    cpu_entry_t *cpu  = NULL;
    cpu_entry_t *next_cpu = NULL;
    list_head_t *head = NULL;

    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->apic_id == apic_id)
        {
            linked_list_remove(head, &cpu->node);
            *cpu_out = cpu;
            return(0);
        }
        cpu = next_cpu;
    }

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

    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->apic_id == apic_id)
        {
            *cpu_out = cpu;
            return(0);
        }
        cpu = next_cpu;
    }
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

}


virt_addr_t *cpu_prepare_trampoline(virt_addr_t *stack_base)
{
    virt_size_t tr_size  = 0;
    uint8_t     *tr_code  = NULL;
    phys_addr_t *pt_base  = 0;
    uint8_t     *pml5_on = NULL;
    uint8_t     *nx_on = NULL;
    virt_addr_t *stack = NULL;
    
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
    pt_base = ((virt_addr_t)&__start_ap_pt_base - _TRAMPOLINE_BEGIN) + tr_code;
    stack   = ((virt_addr_t)&__start_ap_stack   - _TRAMPOLINE_BEGIN) + tr_code;


    pml5_on[0] = pagemgr_pml5_support();
    nx_on[0]   = pagemgr_nx_support();
    pt_base[0] = __read_cr3();
    stack[0]   = (virt_addr_t)stack_base;
}

int cpu_setup(void)
{
    uint8_t      cpu_is_bsp = 0;
    uint32_t     cpu_id     = 0;
    cpu_entry_t *cpu        = NULL;
    virt_size_t stack_size  = 0;
    int status = 0;

    cpu_id     = apic_id_get();
    cpu_is_bsp = apic_is_bsp();

    status = cpu_add_entry(cpu_id, &cpu);
    stack_size = _BSP_STACK_BASE - _BSP_STACK_TOP;

    if(status == -1)
    {
        kprintf("Cannot add CPU %d to list\n",cpu_id);
        return(status);
    }
    else if(status == 1)
    {
        kprintf("CPU %d already initialized\n",cpu_id);
        return(status);
    }

    cpu->apic_address = apic_get_phys_addr();

    if(cpu_is_bsp)
    {
        /* TODO: USE GUARD PAGES */
        cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                                  stack_size, 
                                                  VMM_ATTR_WRITABLE | 
                                                  VMM_GUARD_MEMORY);

        cpu->stack_bottom = cpu->stack_top + stack_size;
        memset((void*)cpu->stack_top, 0, stack_size);
    }
    else
    {
        cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                                  stack_size, 
                                                  VMM_ATTR_WRITABLE | 
                                                  VMM_GUARD_MEMORY);

        cpu->stack_bottom = cpu->stack_top + stack_size;
        memset((void*)cpu->stack_top, 0, stack_size);
    }

    
    if(cpu_is_bsp)
    {
        cpu_stack_relocate((virt_addr_t*)cpu->stack_bottom,
                           (virt_addr_t*)_BSP_STACK_BASE);
    }
    else
    {
        cpu_prepare_trampoline(cpu->stack_bottom);
    }

    return(0);
}


void cpu_entry_point(void)
{
    kprintf("Started CPU %d\n",apic_id_get());
    while(1)
    {
        halt();
    }
}