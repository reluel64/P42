#include <stddef.h>
#include <vm.h>
#include <gdt.h>
#include <utils.h>
#include <platform.h>

extern void __lgdt(void *gdt);
extern void __ltr(uint64_t segment);
extern void __flush_gdt(void);

#define GDT_ENTRY_SIZE (sizeof(gdt_entry_t))
#define TSS_ENTRY_SIZE (sizeof(tss64_entry_t))

#define GDT_COUNT (16)
#define GDT_TABLE_SIZE (GDT_COUNT * GDT_ENTRY_SIZE)
#define GDT_TABLE_LIMIT (GDT_TABLE_SIZE - 1)
#define TSS_SEGMENT (0x28)

#define DESC_MEM_ALLOC (GDT_TABLE_SIZE + TSS_ENTRY_SIZE)

static int gdt_entry_encode
(
    uint64_t base,
    uint32_t limit,
    uint32_t flags,
    gdt_entry_t *gdt_entry
)
{
    /* No way */
    if (gdt_entry == NULL)
        return (-1);

    memset(gdt_entry, 0, sizeof(gdt_entry_t));

    /* set the base linear address */
    gdt_entry->base_high = ((base >> 24) & 0xff);
    gdt_entry->base_mid = ((base >> 16) & 0xff);
    gdt_entry->base_low = ((base)&0xffff);

    /* setup the flags - 7:15 */
    gdt_entry->type = (flags & 0xf);
    gdt_entry->desc_type = ((flags >> 4) & 0x1);
    gdt_entry->dpl = ((flags >> 5) & 0x3);
    gdt_entry->present = ((flags >> 7) & 0x1);

    /* setup the flags 19:23 */
    gdt_entry->avl = ((flags >> 12) & 0x1);
    gdt_entry->_long = ((flags >> 13) & 0x1);
    gdt_entry->def_op_sz = ((flags >> 14) & 0x1);
    gdt_entry->granularity = ((flags >> 15) & 0x1);

    /* set limits */
    gdt_entry->limit_low = ((limit)&0xffff);
    gdt_entry->limit_hi = ((limit >> 16) & 0xf);

    return (0);
}

int gdt_per_cpu_init(void *cpu_pv)
{
    cpu_platform_t *cpu      = NULL;
    uint32_t        flags    = 0;
    gdt_entry_t    *gdt      = NULL;
    tss64_entry_t  *tss      = NULL;
    gdt_ptr_t       gdt_ptr  = {.limit = 0, .addr = 0};
    uint8_t        *desc_mem = NULL;

    desc_mem = (uint8_t*)vm_alloc(NULL, VM_BASE_AUTO, GDT_TABLE_SIZE, VM_HIGH_MEM, VM_ATTR_WRITABLE);

    cpu = cpu_pv;

    gdt = (gdt_entry_t *)desc_mem;
    tss = (tss64_entry_t*)(desc_mem + GDT_TABLE_SIZE);

    if (gdt == NULL || tss == NULL)
        return (-1);

    memset(desc_mem, 0, DESC_MEM_ALLOC);

    /* Kernel Code (0x8)*/
    flags = (GDT_TYPE_SET(GDT_CODE_XR)                  |
             GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
             GDT_PRESENT_SET(0x1)                       |
             GDT_LONG_SET(0x1)                          |
             GDT_GRANULARITY_SET(0x1));

    gdt_entry_encode(0, UINT32_MAX, flags, &gdt[1]);

    /* Kernel Data (0x10)*/
    flags = (GDT_TYPE_SET(GDT_DATA_RW)                  |
             GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA) |
             GDT_PRESENT_SET(0x1)                       |
             GDT_GRANULARITY_SET(0x1));

    gdt_entry_encode(0, UINT32_MAX, flags, &gdt[2]);

    /* User Code (0x18)*/
    flags = GDT_TYPE_SET(GDT_CODE_XR)                   |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA)  |
            GDT_PRESENT_SET(0x1)                        |
            GDT_DPL_SET(0x3)                            |
            GDT_LONG_SET(0x1)                           |
            GDT_GRANULARITY_SET(0x1);

    gdt_entry_encode(0, UINT32_MAX, flags, &gdt[3]);

    /* User Data (0x20)*/
    flags = GDT_TYPE_SET(GDT_DATA_RW)                   |
            GDT_DESC_TYPE_SET(GDT_DESC_TYPE_CODE_DATA)  |
            GDT_DPL_SET(0x3)                            |
            GDT_PRESENT_SET(0x1)                        |
            GDT_GRANULARITY_SET(0x1);

    gdt_entry_encode(0, UINT32_MAX, flags, &gdt[4]);

    /* TSS descriptor (0x28) */
    flags = (GDT_TYPE_SET(GDT_SYSTEM_TSS)               |
             GDT_GRANULARITY_SET(0x1)                   |
             GDT_DPL_SET(0x0)                           |
             GDT_DESC_TYPE_SET(GDT_DESC_TYPE_SYSTEM) |
             GDT_PRESENT_SET(0x1));

    tss->io_map = sizeof(tss);


    gdt_entry_encode((uint64_t)tss, sizeof(tss64_entry_t) - 1, flags, &gdt[5]);

    /* Yes, TSS takes two GDT entrties */
    ((uint64_t *)gdt)[6] = (((uint64_t)tss) >> 32);

    cpu->gdt = gdt;
    cpu->tss = tss;

    gdt_ptr.addr = (virt_addr_t)gdt;
    gdt_ptr.limit = GDT_TABLE_LIMIT;

    __lgdt(&gdt_ptr);
    __ltr(TSS_SEGMENT);
    __flush_gdt();

    kprintf("GDT 0x%x TSS 0x%x\n", gdt, tss);
    return (0);
}

void gdt_update_tss
(
    void *cpu_pv, 
    virt_addr_t esp0
)
{
    cpu_platform_t *cpu = NULL;

    cpu = cpu_pv;

    cpu->tss->rsp0_low = esp0 & UINT32_MAX;
    cpu->tss->rsp0_high = (esp0 >> 32) & UINT32_MAX;
}