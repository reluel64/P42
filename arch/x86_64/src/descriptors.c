/* Descriptors routines */

#include <descriptors.h>
#include <utils.h>
/* Task state segment */ 
static tss64_entry_t tss;
#if 0
static uint64_t interupt_handlers[MAX_INTERRUPTS];

/* Global Descriptor Table */
static gdt_entry_t gdt[MAX_GDT_ENTRIES];
#endif
/* Interrupt Descriptor Pointer */
static idt64_ptr_t idt_ptr;

/* Global Descriptor Pointer */
static gdt64_ptr_t gdt_ptr;

/* extern */
extern void load_idt(void *idt);
extern void load_gdt(void *gdt);
extern void load_tss(uint64_t segment);
extern void disable_interrupts();
extern void enable_interrupts();
extern void isr_handlers_fill(uint64_t *int_hnd);


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

    if(idt_entry == (void*)0)
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

int gdt_entry_encode
(
    uint64_t base, 
    uint32_t limit,
    uint32_t flags,
    gdt_entry_t *gdt_entry
)
{
    /* No way */
    if(gdt_entry == (void*)0)
        return(-1);

    memset(gdt_entry,0,sizeof(gdt_entry_t));

    /* set the base linear address */
    gdt_entry->base_high = ((base >> 24) & 0xff);
    gdt_entry->base_mid  = ((base >> 16) & 0xff);
    gdt_entry->base_low  = ((base) & 0xffff);

    /* setup the flags - 7:15 */
    gdt_entry->type      = (flags & 0xf);
    gdt_entry->desc_type = ((flags >> 4) & 0x1);
    gdt_entry->dpl       = ((flags >> 5) & 0x3);
    gdt_entry->present   = ((flags >> 7) & 0x1);

    /* setup the flags 19:23 */
    gdt_entry->avl         = ((flags >> 12) & 0x1);
    gdt_entry->_long       = ((flags >> 13) & 0x1);
    gdt_entry->def_op_sz   = ((flags >> 14) & 0x1);
    gdt_entry->granularity = ((flags >> 15) & 0x1); 

    /* set limits */
    gdt_entry->limit_low = ((limit) & 0xffff);
    gdt_entry->limit_hi  = ((limit >> 16) & 0xf);

    return(0);
}
#if 0
int setup_descriptors(void)
{
  
    uint32_t flags = 0;
    uint64_t tss_addr = (uint64_t)&tss;


    /* Kernel Code (0x8)*/
    flags = (GDT_TYPE_SET(GDT_CODE_XR)                 |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_PRESENT_SET(0x1)                       |
            GDT_LONG_SET(0x1)                          |
            GDT_GRANULARITY_SET(0x1));

    gdt_entry_encode(0,-1, flags, &gdt[1]);

    /* Kernel Data (0x10)*/
    flags = (GDT_TYPE_SET(GDT_DATA_RW)                 |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_PRESENT_SET(0x1)                       |
            GDT_GRANULARITY_SET(0x1));
 
    gdt_entry_encode(0,-1, flags, &gdt[2]);

    /* User Code (0x18)*/
    flags = GDT_TYPE_SET(GDT_CODE_XR)                  |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_PRESENT_SET(0x1)                       |
            GDT_DPL_SET (0x3)                          |
            GDT_LONG_SET(0x1)                          |
            GDT_GRANULARITY_SET(0x1);

    gdt_entry_encode(0,-1, flags, &gdt[3]);

    /* User Data (0x20)*/
    flags = GDT_TYPE_SET(GDT_DATA_RW)                  |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_DPL_SET(0x3)                           |
            GDT_PRESENT_SET(0x1)                       |
            GDT_GRANULARITY_SET(0x1);

    gdt_entry_encode(0,-1, flags, &gdt[4]);

    /* TSS descriptor*/
    flags = (GDT_TYPE_SET(GDT_SYSTEM_TSS)              | 
            GDT_GRANULARITY_SET(0x1)                   |
            GDT_DPL_SET(0x0)                           |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_SYSTEM)    |
            GDT_PRESENT_SET(0x1));
   
    tss.io_map = sizeof(tss);

    gdt_entry_encode(tss_addr,sizeof(tss)-1, flags, &gdt[5]);
    /* Yes, TSS takes two GDT entrties */
   *((uint32_t*)&gdt[6]) = (tss_addr >> 32);

    isr_handlers_fill(interupt_handlers);

    /* Let's do a check */
    for(int i = 0; i < MAX_INTERRUPTS;i++)
    {
        if(interupt_handlers[i] == 0)
        {
            while(1);
        }
    }


    /* Set up interrupt handlers */
    for(int i = 0; i < MAX_INTERRUPTS; i++)
    {
     idt_entry_add(
    (interrupt_handler_t)interupt_handlers[i],  /* interrupt handler              */
     0x8E,                                      /* this is an interrupt           */
     0,                                         /* no IST                         */
     KERNEL_CODE_SEGMENT,                       /* isr must run in Kernel Context */
     &idt[i]                                    /* position in the IDT            */
     );
    }

    return(0);
}
#endif
int load_descriptors()
{
    /* turn off interrupts */

  //  gdt_ptr.addr = (uint64_t)&gdt;
   // gdt_ptr.len  = sizeof(gdt) - 1;

    //idt_ptr.addr = (uint64_t)&idt;
   // idt_ptr.len = sizeof(idt) - 1;

    /* diable interrupts before trying to change the tables */
    disable_interrupts();

    load_gdt(&gdt_ptr);
    load_idt(&idt_ptr);

    load_tss(TSS_SEGMENT);
    
    /* re-enable them to make the system tick */
    enable_interrupts();

    return(0);
}