/* x86 interrupt handling core
 * Part of P42 
 */ 

#include <vmmgr.h>
#include <isr.h>
#include <utils.h>
#include <gdt.h>
#include <linked_list.h>
#include <liballoc.h>
#include <platform.h>
typedef struct isr_t
{
    list_node_t node;
    interrupt_handler_t ih;
    void *pv;
}isr_t;

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
    spinlock_init(&eoi_lock);
    return(0);
}

int isr_install
(
    interrupt_handler_t ih, 
    void *pv, 
    uint16_t index, 
    uint8_t  eoi
)
{
    isr_t *intr = NULL;
    int int_status = 0;

    if(index >= MAX_HANDLERS  && eoi == 0)
        return(-1);

    intr = kmalloc(sizeof(isr_t));
   
    if(intr == NULL)
        return(-1);
    
    intr->ih = ih;
    intr->pv = pv;

    if(!eoi)
    {   
        spinlock_lock_interrupt(&handlers[index].lock, &int_status);

        linked_list_add_head(&handlers[index].head, &intr->node);

        spinlock_unlock_interrupt(&handlers[index].lock, int_status);
    }
    else
    {
        spinlock_lock_interrupt(&eoi_lock, &int_status);

        linked_list_add_head(&eoi_handlers, &intr->node);

        spinlock_unlock_interrupt(&eoi_lock, int_status);
    }
    return(0);
}

int isr_uninstall
(
    interrupt_handler_t ih,
    void *pv,
    uint8_t eoi
)
{
    isr_t       *intr      = NULL;
    int         int_status = 0;
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;    
    isr_list_t  *isr_lst    = NULL;

    if(eoi)
    {
        spinlock_lock_interrupt(&eoi_lock, &int_status);

        node = linked_list_first(&eoi_handlers);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (isr_t*)node;
            
            if(intr->ih == ih && intr->pv == pv)
            {
                linked_list_remove(&eoi_handlers, node);

                kfree(intr);
            }

            node = next_node;
        }
        spinlock_unlock_interrupt(&eoi_lock, int_status);

        return(0);
    }

    for(uint16_t i = 0; i <  MAX_HANDLERS; i++)
    {
        isr_lst = &handlers[i];
        
        spinlock_lock_interrupt(&isr_lst->lock, &int_status);
        
        node = linked_list_first(&isr_lst->head);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (isr_t*)node;

            if(intr->ih == ih)
            {

                linked_list_remove(&isr_lst->head, node);

                kfree(intr);
            }

            node = next_node;
        }
    }
    return(0);
}

void isr_dispatcher(uint64_t index, virt_addr_t iframe)
{
    int               status     = 0;
    isr_list_t        *int_lst   = NULL;
    isr_t             *intr      = NULL;
    list_node_t       *node      = NULL;
    interrupt_frame_t *int_frame = 0;
    int               int_status = 0;

    if(index >= MAX_HANDLERS)
        return;

    int_lst = &handlers[index];


    /* gain exclusive access to the list */
    spinlock_lock_interrupt(&int_lst->lock, &int_status);

    node = linked_list_first(&int_lst->head);

    while(node)
    {
        intr = (isr_t*)node;

        if(intr->ih)
            status = intr->ih(intr->pv, iframe);

        if(!status)
        {
            break;
        }

        node = linked_list_next(node);
    }

    spinlock_unlock_interrupt(&int_lst->lock, int_status);

    spinlock_lock_interrupt(&eoi_lock, &int_status);

    /* Send EOIs */
    node = linked_list_first(&eoi_handlers);

    while(node)
    {
        intr = (isr_t*)node;

        if(intr->ih)
            status = intr->ih(intr->pv, iframe);

        if(!status)
        {
            spinlock_unlock_interrupt(&eoi_lock, int_status);
            return;
        }
        node = linked_list_next(node);
    }

    spinlock_unlock_interrupt(&eoi_lock, int_status);
}
