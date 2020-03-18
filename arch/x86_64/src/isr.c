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

typedef struct
{
    idt64_entry_t *idt;
    idt64_ptr_t    idt_ptr;
}isr_root_t;

static isr_root_t isr;

extern uint64_t isr_no_ec_begin;
extern uint64_t isr_no_ec_end;
extern uint64_t isr_ec_begin;
extern uint64_t isr_ec_end;
extern uint64_t isr_ec_b;
extern uint64_t isr_ec_e;
extern uint64_t isr_no_ec_sz_start;
extern uint64_t isr_no_ec_sz_end;
extern uint64_t isr_ec_sz_start;
extern uint64_t isr_ec_sz_end;

extern void load_idt(void *idtr);

int idt_entry_add
(
    interrupt_handler_t ih,
    uint8_t type_attr,
    uint8_t ist,
    uint16_t selector,
    idt64_entry_t *idt_entry
)
{
   uint64_t ih_ptr = 0;

    if(idt_entry == NULL)
        return(-1);

    ih_ptr = (uint64_t) ih; /* makes things easier */

    /* set address of the handler */
    idt_entry->offset_1 = (ih_ptr & 0xffff);
    idt_entry->offset_2 = (ih_ptr & 0xffff0000) >> 16 ;
    idt_entry->offset_3 = (ih_ptr & 0xffffffff00000000) >> 32;

    /* set type, attributes and selector */
    idt_entry->seg_selector = selector;
    idt_entry->type_attr = type_attr;

    return(0);
}

int init_isr(void)
{
    idt64_entry_t      *idt = NULL;
    uint32_t            isr_size = 0;
    uint16_t            no_ec_ix = 0;
    uint16_t            ec_ix    = 0;
    interrupt_handler_t ih = NULL;

    memset(&isr, 0, sizeof(isr_root_t));

    isr.idt = vmmgr_alloc(IDT_TABLE_SIZE,VMM_ATTR_WRITABLE);
    
    if(isr.idt == NULL)
        return(-1);

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
                isr_size = (uint64_t)&isr_ec_sz_end - 
                           (uint64_t)&isr_ec_sz_start;

                ih = (interrupt_handler_t)(uint64_t)&isr_ec_begin + 
                                                    (ec_ix * isr_size);
                ec_ix++;
            }
            else
            {
                isr_size = (uint64_t)&isr_no_ec_sz_end - 
                           (uint64_t)&isr_no_ec_sz_start;

                ih = (interrupt_handler_t)(uint64_t)&isr_no_ec_begin + 
                                                    (no_ec_ix * isr_size);
                no_ec_ix++;
            }
        }
        else
        {
            isr_size = (uint64_t)&isr_no_ec_sz_end - 
                       (uint64_t)&isr_no_ec_sz_start;

            ih = (interrupt_handler_t)(uint64_t)&isr_no_ec_begin + 
                                                (no_ec_ix * isr_size);
            no_ec_ix++;
        }

        idt_entry_add(ih,                  /* interrupt handler              */
                      0x8E,                /* this is an interrupt           */
                      0,                   /* no IST                         */
                      KERNEL_CODE_SEGMENT, /* isr must run in Kernel Context */
                      &idt[i]              /* position in the IDT            */
                     );
    }

    isr.idt_ptr.addr = (uint64_t)isr.idt;
    isr.idt_ptr.len = IDT_TABLE_SIZE - 1;

    load_idt(&isr.idt_ptr);
    return(0);
}

extern uint64_t read_cr2();

void isr_handler(uint64_t index, uint64_t error_code)
{
    uint64_t addr = 0;
    addr = read_cr2();
    kprintf("Addr %d -  ERR %d - 0x%x\n",index,error_code, addr);
}
