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
typedef struct interrupt_t
{
    list_node_t node;
    interrupt_handler_t ih;
    void *pv;
}interrupt_t;

static list_head_t handlers[MAX_HANDLERS];
static list_head_t eoi_handlers;
static spinlock_t  lock;



int isr_init(void)
{
    memset(&handlers, 0, sizeof(handlers));
    linked_list_init(&eoi_handlers);
    spinlock_init(&lock);
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
    interrupt_t *intr = NULL;

    if(index >= MAX_HANDLERS  && eoi == 0)
        return(-1);

    intr = kmalloc(sizeof(interrupt_t));
   
    if(intr == NULL)
        return(-1);
    
    intr->ih = ih;
    intr->pv = pv;

    if(!eoi)
        linked_list_add_head(&handlers[index], &intr->node);
    else
        linked_list_add_head(&eoi_handlers, &intr->node);

    return(0);
}

int isr_uninstall
(
    interrupt_handler_t ih,
    void *pv,
    uint8_t eoi
)
{
    interrupt_t *intr = NULL;
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;    
    list_head_t *int_lh = NULL;

    if(eoi)
    {
        node = linked_list_first(&eoi_handlers);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (interrupt_t*)node;
            
            if(intr->ih == ih && intr->pv == pv)
            {

                linked_list_remove(int_lh, node);

                kfree(intr);
            }

            node = next_node;
        }

        return(0);
    }

    for(uint16_t i = 0; i <  MAX_HANDLERS; i++)
    {
        int_lh = &handlers[i];
        node = linked_list_first(int_lh);

        while(node)
        {
            next_node = linked_list_next(node);

            intr = (interrupt_t*)node;

            if(intr->ih == ih)
            {

                linked_list_remove(int_lh, node);

                kfree(intr);
            }

            node = next_node;
        }
    }
    return(0);
}


void isr_dispatcher(uint64_t index, uint64_t error_code, uint64_t ip)
{
    int status = 0;
    list_head_t *int_lh = NULL;
    interrupt_t *intr = NULL;
    list_node_t *node = NULL;
    
    if(index >= MAX_HANDLERS)
        return;

    /*kprintf("INTERRUPT 0x%x EC %x CPUID %d\n",index, error_code, cpu_id_get());*/

    int_lh = &handlers[index];

    node = linked_list_first(int_lh);

    while(node)
    {
        intr = (interrupt_t*)node;

        if(intr->ih)
            status = intr->ih(intr->pv, error_code);


        if(!status)
        {
            break;
        }

        node = linked_list_next(node);
    }

    int_lh = &eoi_handlers;

    /* Send EOIs */
    node = linked_list_first(int_lh);
 //   kprintf("SEND EOI\n");
    while(node)
    {
        intr = (interrupt_t*)node;

        if(intr->ih)
            status = intr->ih(intr->pv, error_code);

        if(!status)
        {
            return;
        }
        node = linked_list_next(node);
    }
}

