#include <stddef.h>
#include <vmmgr.h>
#include <gdt.h>
#include <utils.h>

typedef struct
{
    gdt_entry_t *gdt;
    gdt64_ptr_t  gdt_ptr;
    tss64_entry_t *tss;
}gdt_t;

extern void _lgdt(void *gdt);
extern void _ltr(uint64_t segment);
extern void _flush_gdt(void);
static gdt_t gdt_root;

#define GDT_ENTRY_SIZE (sizeof(gdt_entry_t))
#define MAX_GDT_COUNT (8192)
#define MAX_GDT_TABLE_SIZE (MAX_GDT_COUNT * GDT_ENTRY_SIZE)

static int gdt_entry_encode
(
    uint64_t base, 
    uint32_t limit,
    uint32_t flags,
    gdt_entry_t *gdt_entry
)
{
    /* No way */
    if(gdt_entry == NULL)
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

int gdt_init(void)
{
    uint32_t     flags = 0;
    gdt_entry_t   *gdt   = NULL;
    tss64_entry_t *tss = NULL;
    memset(&gdt_root, 0, sizeof(gdt_t));
    kprintf("GDT INIT\n");
    gdt_root.gdt = (gdt_entry_t*)vmmgr_alloc(0, MAX_GDT_TABLE_SIZE, VMM_ATTR_WRITABLE);
    gdt_root.tss = (tss64_entry_t*) vmmgr_alloc(0, sizeof(tss64_entry_t),VMM_ATTR_WRITABLE);

    if(gdt_root.gdt == NULL || gdt_root.tss == NULL)
        return(-1);

    gdt = gdt_root.gdt;
    tss = gdt_root.tss;

    memset(gdt, 0, MAX_GDT_TABLE_SIZE);
    memset(tss, 0, sizeof(tss64_entry_t));

        /* Kernel Code (0x8)*/
    flags = (GDT_TYPE_SET(GDT_CODE_XR)                 |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_PRESENT_SET(0x1)                       |
            GDT_LONG_SET(0x1)                          |
            GDT_GRANULARITY_SET(0x1));

    gdt_entry_encode(0,~0, flags, &gdt[1]);

    /* Kernel Data (0x10)*/
    flags = (GDT_TYPE_SET(GDT_DATA_RW)                 |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_PRESENT_SET(0x1)                       |
            GDT_GRANULARITY_SET(0x1));
 
    gdt_entry_encode(0,~0, flags, &gdt[2]);

    /* User Code (0x18)*/
    flags = GDT_TYPE_SET(GDT_CODE_XR)                  |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_PRESENT_SET(0x1)                       |
            GDT_DPL_SET (0x3)                          |
            GDT_LONG_SET(0x1)                          |
            GDT_GRANULARITY_SET(0x1);

    gdt_entry_encode(0,~0, flags, &gdt[3]);

    /* User Data (0x20)*/
    flags = GDT_TYPE_SET(GDT_DATA_RW)                  |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
            GDT_DPL_SET(0x3)                           |
            GDT_PRESENT_SET(0x1)                       |
            GDT_GRANULARITY_SET(0x1);

    gdt_entry_encode(0,-1, flags, &gdt[4]);

    /* TSS descriptor (0x28) */
    flags = (GDT_TYPE_SET(GDT_SYSTEM_TSS)              | 
            GDT_GRANULARITY_SET(0x1)                   |
            GDT_DPL_SET(0x0)                           |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_SYSTEM)    |
            GDT_PRESENT_SET(0x1));

    tss->io_map = sizeof(tss);
    
    gdt_entry_encode((uint64_t)tss,sizeof(tss)-1, flags, &gdt[5]);
    /* Yes, TSS takes two GDT entrties */
   ((uint64_t*)gdt)[6] = (((uint64_t)tss) >> 32);


    gdt_root.gdt_ptr.addr = (uint64_t)gdt;
    gdt_root.gdt_ptr.len  = MAX_GDT_TABLE_SIZE - 1; 

    _lgdt(&gdt_root.gdt_ptr);
    extern void _ti();
      _ltr(TSS_SEGMENT);
   _flush_gdt();

  // _ti();
  
   // _test_interrupt();
kprintf("GDT 0x%x TSS 0x%x\n",gdt_root.gdt, gdt_root.tss);
    return(0);
}




virt_addr_t gdt_base_get(void)
{
    return(&gdt_root.gdt_ptr);
}