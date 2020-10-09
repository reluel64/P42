/* x86 interrupt handling core
 * Part of P42 
 */ 

#include <vmmgr.h>
#include <isr.h>
#include <utils.h>
#include <gdt.h>
#define IDT_ENTRY_SIZE (sizeof(idt64_entry_t))
#define IDT_TABLE_COUNT (256)
#define IDT_TABLE_SIZE (IDT_ENTRY_SIZE * IDT_TABLE_COUNT)
#define ISR_EC_MASK (0x27D00)
#define RESERVED_ISR_BEGIN (21)
#define RESERVED_ISR_END   (31)
#define MAX_HANDLERS       (256)

typedef struct
{
    uint16_t isr_ix;
    interrupt_handler_t ih;
    void *pv;
}interrupt_t;

typedef struct
{
    idt64_entry_t *idt;
    idt64_ptr_t    idt_ptr;
    interrupt_t   *handlers;
}isr_root_t;

static isr_root_t isr;

extern virt_addr_t isr_no_ec_begin;
extern virt_addr_t isr_no_ec_end;
extern virt_addr_t isr_ec_begin;
extern virt_addr_t isr_ec_end;
extern virt_addr_t isr_ec_b;
extern virt_addr_t isr_ec_e;
extern virt_addr_t isr_no_ec_sz_start;
extern virt_addr_t isr_no_ec_sz_end;
extern virt_addr_t isr_ec_sz_start;
extern virt_addr_t isr_ec_sz_end;
extern virt_addr_t dummy_interrupt;
extern void __lidt(void *idtr);
extern void __sti();
extern void __cli();

static int idt_entry_add
(
    uint64_t ih,
    uint8_t type_attr,
    uint8_t ist,
    uint16_t selector,
    idt64_entry_t *idt_entry
)
{
    
    if(idt_entry == NULL)
        return(-1);

    /* set address of the handler */
    idt_entry->offset_1 = (ih & 0xffff);
    idt_entry->offset_2 = (ih & 0xffff0000) >> 16 ;
    idt_entry->offset_3 = (ih & 0xffffffff00000000) >> 32;

    /* set type, attributes and selector */
    idt_entry->seg_selector = selector;
    idt_entry->type_attr = type_attr;

    return(0);
}

int isr_init(void)
{
    idt64_entry_t      *idt = NULL;
    uint32_t            isr_size = 0;
    uint16_t            no_ec_ix = 0;
    uint16_t            ec_ix    = 0;
    virt_addr_t         ih = 0;
    
    kprintf("IDT INIT\n");
    /* diable interrupts while installing the IDT */
    __cli();

    memset(&isr, 0, sizeof(isr_root_t));

    isr.idt = vmmgr_alloc(NULL, 0, IDT_TABLE_SIZE,VMM_ATTR_WRITABLE);

    if(isr.idt == NULL)
        return(-1);

    isr.handlers = vmmgr_alloc(NULL, 0, MAX_HANDLERS * sizeof(interrupt_t), 
                                VMM_ATTR_WRITABLE);

    if(isr.handlers == NULL)
        return(-1);
    
    memset(isr.handlers, 0, MAX_HANDLERS * sizeof(interrupt_t));

    memset(isr.idt, 0, IDT_TABLE_SIZE);
    idt = isr.idt;
    
    /* Set up interrupt handlers */
    for(uint16_t i = 0; i < IDT_TABLE_COUNT; i++)
    {

        if(i >= RESERVED_ISR_BEGIN && i <=RESERVED_ISR_END || i == 15)
            continue;
            
        else if(i < RESERVED_ISR_BEGIN)
        {
            if((1 << i) & ISR_EC_MASK)
            {
                isr_size = (virt_addr_t)&isr_ec_sz_end - 
                           (virt_addr_t)&isr_ec_sz_start;

                ih = (virt_addr_t)&isr_ec_begin + 
                                (ec_ix * isr_size);
                ec_ix++;
            }
            else
            {
                isr_size = (virt_addr_t)&isr_no_ec_sz_end - 
                           (virt_addr_t)&isr_no_ec_sz_start;

                ih = (virt_addr_t)&isr_no_ec_begin + 
                                (no_ec_ix * isr_size);
                no_ec_ix++;
            }
        }
        else
        {
            isr_size = (virt_addr_t)&isr_no_ec_sz_end - 
                       (virt_addr_t)&isr_no_ec_sz_start;

            ih = (virt_addr_t)&isr_no_ec_begin + 
                            (no_ec_ix * isr_size);
            no_ec_ix++;
        }

        idt_entry_add(ih,                                       /* interrupt handler              */
                      GDT_PRESENT_SET(1) | 
                      GDT_TYPE_SET(GDT_SYSTEM_INTERUPT_GATE),  /* this is an interrupt           */
                      0,                   /* no IST                         */
                      KERNEL_CODE_SEGMENT, /* isr must run in Kernel Context */
                      &idt[i]              /* position in the IDT            */
                     );
    }

    isr.idt_ptr.addr = (virt_addr_t)isr.idt;
    isr.idt_ptr.limit = IDT_TABLE_SIZE - 1;

    return(0);
}

void isr_per_cpu_init(void)
{
    __lidt(&isr.idt_ptr);
}


int isr_install(interrupt_handler_t ih, void *pv, uint16_t index)
{
    interrupt_t *intr = NULL;

    intr = isr.handlers;

    for(uint16_t i = 0; i <  MAX_HANDLERS; i++)
    {
        if(intr[i].ih == NULL)
        {
            intr[i].ih = ih;
            intr[i].isr_ix = index;
            intr[i].pv = pv;
            return(0);
        }
    }
    return(-1);
}

int isr_uninstall(interrupt_handler_t ih)
{
    interrupt_t *intr = NULL;

    intr = isr.handlers;

    for(uint16_t i = 0; i <  MAX_HANDLERS; i++)
    {
        if(intr[i].ih == ih)
        {
            intr[i].ih = NULL;
            intr[i].isr_ix = 0;
            intr[i].pv = NULL;
        }
    }
    return(-1);
}


void isr_dispatcher(uint64_t index, uint64_t error_code, uint64_t ip)
{
    int status = 0;
    interrupt_t *intr = NULL;
   // kprintf("ERROR 0x%x EC %x\n",index, error_code);
    for(uint16_t i = 0; i < MAX_HANDLERS; i++)
    {
        intr  = isr.handlers + i;

        if(intr->isr_ix == index && intr->ih != NULL)
        {
           
            status = intr->ih(intr->pv, error_code);
            
            if(status == 0)
                break;
        }
    }
}
