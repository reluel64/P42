/* Descriptors routines */

#include "descriptors.h"
/* local */

/* Interrupt Descriptor Table */ 
    static idt64_entry_t idt[MAX_IDTS];

    /* Global Descriptor Table */
    static gdt_entry_t gdt[MAX_GDTS];

    static tss64_entry_t tss;
    static idt64_ptr_t idt_ptr;
    static gdt64_ptr_t gdt_ptr;



/* extern */
extern void load_idt(void *idt);
extern void load_gdt(void *gdt);
extern void load_tss(uint64_t segment);
extern void disable_interrupts();
extern void enable_interrupts();
extern void isr_entry(void);


static void clear(void *ptr, uint64_t len)
{
    for(uint64_t i = 0; i < len; i++)
         ((uint8_t*)ptr)[i] = 0;
}

static void copy(void *dst, void  *src, uint64_t len)
{
    for(uint64_t i = 0; i < len; i++)
         ((uint8_t*)dst)[i] = ((uint8_t*)src)[i];
}

int idt_entry_add
(
    interrupt_handler_t ih,
    uint16_t type_attr,
    uint16_t selector,
    idt64_entry_t *idt_entry
)
{
    uint64_t ih_ptr = 0;

   if(idt_entry == (void*)0)
        return(-1);

   clear(idt_entry, sizeof(idt64_entry_t));

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
#if 0
int gdt_entry_encode
(
    uint64_t base, 
    uint32_t limit,
    uint16_t flags,
    gdt_entry_t *gdt_entry
)
{
    if(gdt_entry == (void*)0)
        return(-1);

    clear(gdt_entry, sizeof(gdt_entry_t));

    gdt_entry->base_high = ((base >> 24) & 0xff);
    gdt_entry->base_mid = ((base >> 16) & 0xff);
    gdt_entry->base_low = ((base >> 0) & 0xffff);

    gdt_entry->limit_low = ((limit >> 0) & 0xffff);
    gdt_entry->flags |= GDT_LIMIT_HIGH((limit >> 16)) |
                             GDT_FLAGS(flags);

    return(0);
}
#endif
int setup_descriptors(void)
{
    
    uint16_t flags = 0;
    uint64_t tss_addr = (uint64_t)&tss;
    uint64_t ih_ptr = (uint64_t)isr_entry;
    /* First segment is NULL */
    clear(gdt, sizeof(gdt));
    clear(idt, sizeof(idt));

    tss.io_map = sizeof(tss);
    gdt[1].flags = ((1 << 1) | (1 << 3) | (1 << 4) | (1 << 7));
    gdt[1].flags2 = (1 << 1) | (1 << 3);
    gdt[2].flags = ((1 << 1) | (1 << 4) | (1 << 7)) ;
    gdt[2].flags2 = (1 << 1) | (1 << 3);

    gdt[3].flags = (1 << 1) | (1 << 7) ;
    gdt[3].flags2 = (1 << 3);

    gdt[3].base_low = tss_addr & 0xffffff;
    gdt[3].base_mid = ((tss_addr >> 16) & 0xff);
    gdt[3].base_high = ((tss_addr >> 24) & 0xff);
    gdt[3].limit_low = sizeof(tss); 
    *((uint32_t*)&gdt[4]) = (tss_addr >> 32);
    

#if 0
    /* Kernel Code (0x8)*/
    flags = GDT_PRESENT(0x1)       | 
            GDT_CODE_READABLE(0x1) | 
            GDT_NON_SYS(0x1)       | 
            GDT_DPL(0x0)           |
            GDT_CODE(0x1)          |
            GDT_CODE_LONG(0x1)     |
            GDT_GRANULARITY(0x1);


    gdt_entry_encode(0,0, flags, &gdt[1]);

    /* Kernel Data (0x10)*/
    flags = GDT_PRESENT(0x1)       | 
            GDT_DATA_WRITABLE(0x1) | 
            GDT_NON_SYS(0x1)       | 
            GDT_DPL(0x0)           |           
            GDT_CODE_LONG(0x1)     |
            GDT_GRANULARITY(0x1);

    gdt_entry_encode(0,0, flags, &gdt[2]);

    /* User Code (0x18)*/
    flags = GDT_PRESENT(0x1)       | 
            GDT_CODE_READABLE(0x1) | 
            GDT_NON_SYS(0x1)       | 
            GDT_DPL(0x3)           |
            GDT_CODE(0x1)          |
            GDT_CODE_LONG(0x1)     |
            GDT_GRANULARITY(0x1);


    gdt_entry_encode(0,0, flags, &gdt[3]);

    /* User Data (0x20)*/
    flags = GDT_PRESENT(0x1)       | 
            GDT_DATA_WRITABLE(0x1) | 
            GDT_NON_SYS(0x1)       | 
            GDT_DPL(0x3)           |           
            GDT_CODE_LONG(0x1)     |
            GDT_GRANULARITY(0x1);

    gdt_entry_encode(0,0, flags, &gdt[4]);

    /* Start encoding the tss descriptor */

    flags = GDT_PRESENT(0x1)     | 
            GDT_GRANULARITY(0x1) |
            GDT_DPL(0x0);
            GDT_NON_SYS(0x0)|
            (1 << 0);
            (1 << 3);
            #endif
#if 0
    clear(&tss,sizeof(tss));
    tss.io_map = sizeof(tss);
    gdt_entry_encode((uint64_t)&tss,sizeof(tss) - 1, flags, &gdt[3]);
    /* Yes, TSS takes two descriptors */
    *((uint32_t*)&gdt[4]) = ((uint64_t)&tss) >> 32;
#endif

    /* Set up interrupt handlers */
    for(int i = 0; i < MAX_IDTS; i++)
    {

    idt[i].offset_1 = ih_ptr & 0xffff;
    idt[i].offset_2 = (ih_ptr & 0xffff0000) >> 16 ;
    idt[i].offset_3 = (ih_ptr & 0xffffffff00000000) >> 32;

    idt[i].type_attr    = 0x8E;
    idt[i].ist          = 0;
    idt[i].zero         = 0;
    idt[i].seg_selector = 0x8;
    idt[i].reserved     = 0;
        //idt_entry_add(isr_entry, 0x8E00,0x8, &idt[i]);
    }



    return(0);
}

int load_descriptors()
{
    /* turn off interrupts */
    disable_interrupts();

    gdt_ptr.addr = (uint64_t)&gdt;
    gdt_ptr.len  = (sizeof(gdt)) - 1;

    idt_ptr.addr = (uint64_t)&idt;
    idt_ptr.len = sizeof(idt);

    load_gdt(&gdt_ptr);

    load_idt(&idt_ptr);
     load_tss(0x18);
    enable_interrupts();

    return(0);
}