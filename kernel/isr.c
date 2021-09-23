/* Interrupt dispatching code
 * Part of P42 
 */ 

#include <vm.h>
#include <isr.h>
#include <utils.h>
#include <gdt.h>
#include <linked_list.h>
#include <liballoc.h>
#include <platform.h>
#include <scheduler.h>

typedef struct isr_list_t
{
    list_head_t head;
    spinlock_t  lock;

}isr_list_t;

static isr_list_t handlers[MAX_HANDLERS];
static list_head_t eoi_handlers;
static spinlock_t  eoi_lock;


int isr_init(void)
{
    memset(&handlers, 0, sizeof(handlers));
    linked_list_init(&eoi_handlers);
    spinlock_rw_init(&eoi_lock);

    for(int i = 0; i < MAX_HANDLERS; i++)
    {
        spinlock_rw_init(&handlers[i].lock);
    }

    return(0);
}

isr_t *isr_install
(
    interrupt_handler_t ih, 
    void *pv, 
    uint16_t index, 
    uint8_t  eoi,
    isr_t *isr_slot
)
{
    isr_t *intr = NULL;
    int int_status = 0;

    if(index >= MAX_HANDLERS  && eoi == 0)
        return(NULL);
    
    if(isr_slot == NULL)
    {
        intr = kmalloc(sizeof(isr_t));
    }
    else
    {
        intr = isr_slot;
    }


    if(intr == NULL)
        return(NULL);

    memset(intr, 0, sizeof(isr_t));

    /* If the isr slot is null, then we have allocated memory for it */
    if(isr_slot == NULL)
        intr->allocated = 1;
        

    intr->ih = ih;
    intr->pv = pv;

    if(!eoi)
    {   
        spinlock_write_lock_int(&handlers[index].lock);

        linked_list_add_head(&handlers[index].head, &intr->node);

        spinlock_write_unlock_int(&handlers[index].lock);
    }
    else
    {
        spinlock_write_lock_int(&eoi_lock);

        linked_list_add_head(&eoi_handlers, &intr->node);

        spinlock_write_unlock_int(&eoi_lock);
    }
    return(intr);
}

int isr_uninstall
(
    isr_t *isr,
    uint8_t eoi
)
{
    isr_t       *intr      = NULL;
    int         int_status = 0;
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;    
    isr_list_t  *isr_lst    = NULL;


    if(isr == NULL)
        return(-1);

    if(eoi)
    {
        spinlock_write_lock_int(&eoi_lock);

        node = linked_list_first(&eoi_handlers);
    
        while(node)
        {
            next_node = linked_list_next(node);

            intr = (isr_t*)node;
            
            if(intr == isr)
            {
                linked_list_remove(&eoi_handlers, node);

                if(intr->allocated)
                    kfree(intr);
            }

            node = next_node;
        }
        spinlock_write_unlock_int(&eoi_lock);

        return(0);
    }

    for(uint16_t i = 0; i <  MAX_HANDLERS; i++)
    {
        isr_lst = &handlers[i];
        
        spinlock_write_lock_int(&isr_lst->lock);
        
        node = linked_list_first(&isr_lst->head);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (isr_t*)node;

            if(isr == intr)
            {
                linked_list_remove(&isr_lst->head, node);
                kfree(intr);
            }

            node = next_node;
        }
        spinlock_write_unlock_int(&isr_lst->lock);
    }
    return(0);
}
   
void isr_dispatcher(uint64_t index, virt_addr_t iframe)
{
    int               status     = 0;
    isr_list_t        *int_lst   = NULL;
    isr_t             *intr      = NULL;
    list_node_t       *node      = NULL;
    cpu_t             *cpu       = NULL;
    int               int_status = 0;
    isr_info_t        inf;
    
    if(index >= MAX_HANDLERS)
        return;


    memset(&inf, 0, sizeof(isr_info_t));
    
    int_lst = &handlers[index];

    inf.iframe = iframe;
    inf.cpu_id = cpu_id_get();
    cpu        = cpu_current_get();

    /* gain exclusive access to the list */
    spinlock_read_lock_int(&int_lst->lock);

    node = linked_list_first(&int_lst->head);

    while(node)
    {
        intr = (isr_t*)node;

        if(intr->ih)
            intr->ih(intr->pv, &inf);

        node = linked_list_next(node);
    }

    spinlock_read_unlock_int(&int_lst->lock);

    spinlock_read_lock_int(&eoi_lock);

    /* Send EOIs */
    node = linked_list_first(&eoi_handlers);

    while(node)
    {
        intr = (isr_t*)node;

        if(intr->ih)
            intr->ih(intr->pv, &inf);

        node = linked_list_next(node);
    }

    spinlock_read_unlock_int(&eoi_lock);

    /* check if we need to reschedule */
    if(cpu && cpu->sched)
    {
        if(sched_need_resched(cpu->sched))
        {
            schedule();
        }
    }
}

