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
static isr_list_t eoi;
static spinlock_t serial_lock;

int isr_init(void)
{
    memset(&handlers, 0, sizeof(handlers));
    linked_list_init(&eoi.head);
    spinlock_rw_init(&eoi.lock);
    spinlock_init(&serial_lock);

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
    uint8_t  is_eoi,
    isr_t *isr_slot
)
{
    isr_t      *intr      = NULL;
    uint8_t    int_status = 0;
    isr_list_t *isr_list  = NULL;

    if((index >= MAX_HANDLERS) && (is_eoi == 0))
    {
        return(NULL);
    }

    if(isr_slot == NULL)
    {
        intr = kmalloc(sizeof(isr_t));

        if(intr == NULL)
        {
            return(NULL);
        }

        intr->allocated = 1;
    }
    else
    {
        intr = isr_slot;
    }

    memset(intr, 0, sizeof(isr_t));    

    intr->ih = ih;
    intr->pv = pv;


    if(is_eoi)
    {
        isr_list = &eoi;
    }
    else
    {
        isr_list = &handlers[index];
    }

    spinlock_write_lock_int(&isr_list->lock, &int_status);

    linked_list_add_head(&isr_list->head, &intr->node);

    spinlock_write_unlock_int(&isr_list->lock, int_status);

    return(intr);
}

int isr_uninstall
(
    isr_t *isr,
    uint8_t is_eoi
)
{
    isr_t       *intr       = NULL;
    uint8_t     int_status  = 0;
    list_node_t *node       = NULL;
    list_node_t *next_node  = NULL;    
    isr_list_t  *isr_lst    = NULL;
    uint16_t    max_index   = 0;

    if(isr == NULL)
    {
        return(-1);
    }

    if(is_eoi)
    {
        max_index = 1;
        isr_lst = &eoi;
    }
    else
    {
        max_index = MAX_HANDLERS;
        isr_lst = handlers;
    }

    for(uint16_t i = 0; i <  max_index; i++)
    {        
        spinlock_write_lock_int(&isr_lst->lock, &int_status);
        
        node = linked_list_first(&isr_lst->head);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (isr_t*)node;

            if(isr == intr)
            {
                linked_list_remove(&isr_lst->head, node);
                
                if(intr->allocated)
                {
                    kfree(intr);
                }
            }

            node = next_node;
        }
        spinlock_write_unlock_int(&isr_lst->lock, int_status);

        isr_lst++;
    }

    return(0);
}

void isr_dispatcher
(
    uint64_t index, 
    virt_addr_t iframe
)
{
    int               status     = 0;
    isr_list_t        *int_lst   = NULL;
    isr_t             *intr      = NULL;
    list_node_t       *node      = NULL;
    cpu_t             *cpu       = NULL;
    int               int_status = 0;
    isr_info_t        inf = {.cpu_id = 0, .iframe = 0};
  
    if(index < MAX_HANDLERS)
    {
        
        int_lst = &handlers[index];

        inf.iframe = iframe;
        inf.cpu_id = cpu_id_get();
        cpu        = cpu_current_get();

        /* gain exclusive access to the list */
        spinlock_read_lock(&int_lst->lock);

        node = linked_list_first(&int_lst->head);

        while(node)
        {
            intr = (isr_t*)node;

            if(intr->ih)
            {
                intr->ih(intr->pv, &inf);
            }

            node = linked_list_next(node);
        }

        spinlock_read_unlock(&int_lst->lock);

        spinlock_read_lock(&eoi.lock);

        /* Send EOIs */
        node = linked_list_first(&eoi.head);

        while(node)
        {
            intr = (isr_t*)node;

            if(intr->ih)
            {
                intr->ih(intr->pv, &inf);
            }
            node = linked_list_next(node);
        }

        spinlock_read_unlock(&eoi.lock);

        /* check if we need to reschedule */
        if(cpu && cpu->sched)
        {
            if(sched_need_resched(cpu->sched))
            {
                schedule();
            }
        }
    }
}

