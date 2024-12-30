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
#include <sched.h>

struct isr_list
{
    struct list_head head;
    struct spinlock_rw  lock;

};



static struct isr_list handlers[MAX_ISR_HANDLERS];
static struct isr_list eoi;
static struct spinlock serial_lock;

int isr_init(void)
{
    memset(&handlers, 0, sizeof(handlers));
    linked_list_init(&eoi.head);
    spinlock_rw_init(&eoi.lock);
    spinlock_init(&serial_lock);

    for(int i = 0; i < MAX_ISR_HANDLERS; i++)
    {
        spinlock_rw_init(&handlers[i].lock);
    }

    return(0);
}

struct isr *isr_install
(
    interrupt_handler_t ih, 
    void *pv, 
    uint16_t index, 
    uint8_t  is_eoi,
    struct isr *isr_slot
)
{
    struct isr      *intr      = NULL;
    uint8_t    int_status = 0;
    struct isr_list *isr_list  = NULL;

    if((index >= MAX_ISR_HANDLERS) && (is_eoi == 0))
    {
        return(NULL);
    }

    if(isr_slot == NULL)
    {
        intr = kmalloc(sizeof(struct isr));

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

    memset(intr, 0, sizeof(struct isr));    

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
    struct isr *isr,
    uint8_t is_eoi
)
{
    struct isr       *intr       = NULL;
    uint8_t     int_status  = 0;
    struct list_node *node       = NULL;
    struct list_node *next_node  = NULL;    
    struct isr_list  *isr_lst    = NULL;
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
        max_index = MAX_ISR_HANDLERS;
        isr_lst = handlers;
    }

    for(uint16_t i = 0; i <  max_index; i++)
    {        
        spinlock_write_lock_int(&isr_lst->lock, &int_status);
        
        node = linked_list_first(&isr_lst->head);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (struct isr*)node;

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
    struct isr_list        *int_lst   = NULL;
    struct isr             *intr      = NULL;
    struct list_node       *node      = NULL;
    struct isr_info        inf = {.cpu_id = 0, .iframe = 0, .cpu = NULL};
    int32_t            st = 0;

    if(index < MAX_ISR_HANDLERS)
    {
        
        int_lst = &handlers[index];

        inf.iframe = iframe;
        inf.cpu_id = cpu_id_get();
        inf.cpu    = cpu_current_get();

        /* gain exclusive access to the list */
        spinlock_read_lock(&int_lst->lock);

        node = linked_list_first(&int_lst->head);

        while(node)
        {
            intr = (struct isr*)node;

            if(intr->ih)
            {
                st = intr->ih(intr->pv, &inf);

                if(st == 0)
                {
                    break;
                }
            }

            node = linked_list_next(node);
        }

        spinlock_read_unlock(&int_lst->lock);

        spinlock_read_lock(&eoi.lock);

        /* Send EOIs */
        node = linked_list_first(&eoi.head);

        while(node)
        {
            intr = (struct isr*)node;

            if(intr->ih)
            {
                st = intr->ih(intr->pv, &inf);
                
                if(st == 0)
                {
                    break;
                }
            }
            node = linked_list_next(node);
        }

        spinlock_read_unlock(&eoi.lock);

        /* check if we need to reschedule */
        if(inf.cpu && inf.cpu->sched)
        {
            if(sched_need_resched(inf.cpu->sched))
            {
                schedule();
            }
        }
    }
}

