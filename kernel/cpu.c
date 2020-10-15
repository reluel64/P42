#include <cpu.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
static list_head_t cpu_list;
static spinlock_t lock;
static cpu_funcs_t *pcpu;


int cpu_add_entry
(
    cpu_entry_t *cpu_in
)
{
    cpu_entry_t *cpu      = NULL;
    cpu_entry_t *next_cpu = NULL;
    list_head_t *head     = NULL;
    int          status   = 0;

    spinlock_lock_interrupt(&lock);
    head = &cpu_list;

    cpu = (cpu_entry_t*)linked_list_first(head);

    /* Check if the CPU is added to the list */
    while(cpu)
    {
        next_cpu = (cpu_entry_t*)linked_list_next((list_node_t*)cpu);

        if(cpu->cpu_id == cpu_in->cpu_id)
        {
            break;
        }
        cpu = next_cpu;
    }


    if(cpu == NULL)
    {
        linked_list_add_tail(head, &cpu_in->node);
    }
    else
    {
        status = 1;
    }
    
    spinlock_unlock_interrupt(&lock);
    
    return(status);
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
    uint32_t cpu_id, 
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

        if(cpu->cpu_id == cpu_id)
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

cpu_entry_t *cpu_get(void)
{
    uint32_t cpu_id  = 0;
    cpu_entry_t *cpu = NULL;

    if(pcpu->cpu_id_get)
        cpu_id = pcpu->cpu_id_get();

    if(cpu_get_entry(cpu_id, &cpu))
        return(NULL);

    return(cpu);
}

int cpu_register_funcs(cpu_funcs_t *func)
{
    if(pcpu == NULL)
        pcpu = func;
    else
       return(-1);
    
    return(0);
}


int cpu_init(void)
{
    int status = 0;
    cpu_entry_t *cpu = NULL;

    spinlock_init(&lock);
    linked_list_init(&cpu_list);
  
    /* set up the bootstrap CPU */
    spinlock_lock_interrupt(&lock);
    
    cpu = kmalloc(sizeof(cpu_entry_t));
    
    memset(cpu, 0, sizeof(cpu_entry_t));
    
    status = pcpu->cpu_setup(cpu);

    if(status == 0)
    {
        spinlock_unlock_interrupt(&lock);  
        cpu_add_entry(cpu);
        
        return(0);
    }

    kfree(cpu);

    spinlock_unlock_interrupt(&lock);
 
    return(-1);
}

int cpu_entry_point(void)
{
    cpu_entry_t *cpu = NULL;
    spinlock_lock_interrupt(&lock);
    
    cpu = cpu_get();
    pcpu->cpu_setup(cpu);
    
    spinlock_unlock_interrupt(&lock);
}

uint32_t cpu_id_get(void)
{
    if(pcpu->cpu_id_get)
        return(pcpu->cpu_id_get());
    else
        return(0);
}

