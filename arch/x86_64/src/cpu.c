#include <cpu.h>
#include <types.h>
#include <utils.h>
#include <apic.h>
#include <liballoc.h>
#include <vmmgr.h>

static list_head_t cpu_list = {.list.next = NULL, 
                               .list.prev = NULL,
                               .count = 0};

extern void __cpu_switch_stack
(
    virt_addr_t *new_top,
    virt_addr_t *new_pos,
    virt_addr_t *old_base
);

extern virt_addr_t __get_rbp();
extern void __set_rbp(virt_addr_t rbp);

extern uint64_t kstack_base;
extern uint64_t kstack_top;
extern virt_addr_t __stack_pointer(void);

#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)

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
    sp     = (virt_addr_t*)__stack_pointer();

    /* Calculate how many entries are from top to base */
    stack_entries = (old_stack_base - sp);
    new_stack_top = new_stack_base - stack_entries;
    old_stack_top = old_stack_base - stack_entries;

    
    kprintf("old_stack_base 0x%x old_stack_top 0x%x\n",old_stack_base, old_stack_top);
    /* Adjust stack content */

    /* Copy the content from the old stack to the new stack */

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

int cpu_init(void)
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
#if 0
        cpu->stack_bottom = _BSP_STACK_BASE;
        cpu->stack_top    = _BSP_STACK_TOP;

        vmmgr_change_attrib(NULL, cpu->stack_bottom, stack_size, ~VMM_ATTR_EXECUTABLE);
#else
        cpu->stack_top = (virt_addr_t)vmmgr_alloc(NULL, 0x0, stack_size, VMM_ATTR_WRITABLE);
        cpu->stack_bottom = cpu->stack_top + stack_size;
        memset((void*)cpu->stack_top, 0, stack_size);

        cpu_stack_relocate((virt_addr_t*)cpu->stack_bottom,
                         (virt_addr_t*)_BSP_STACK_BASE);
#endif
    }
    else
    {
        cpu->stack_bottom = (virt_addr_t)vmmgr_alloc(NULL, 0x0, stack_size, VMM_ATTR_WRITABLE);
        cpu->stack_top = cpu->stack_bottom + stack_size;
        memset((void*)cpu->stack_bottom, 0,stack_size);
    }

    uint64_t rsp =cpu->stack_top;

    for(uint64_t *ptr = rsp; ptr < cpu->stack_bottom ; ptr++)
    {
        if(ptr[0] >= cpu->stack_top && ptr[0] <= cpu->stack_bottom )
        {
            kprintf("FRAME 0x%x OFFSET 0x%x\n",ptr[0], cpu->stack_bottom - ptr[0]);
        }
    }


    kprintf("RSP 0x%x\n", rsp);
    return(0);
}