/* Paging management 
 * This file contains code that would 
 * allow us to manage paging 
 * structures
 */


#include <stdint.h>
#include <paging.h>
#include <utils.h>
#include <pagemgr.h>
#include <isr.h>
#include <spinlock.h>
#include <vmmgr.h>
#include <platform.h>
#include <intc.h>
#include <pfmgr.h>

typedef struct pagemgr_root_t
{
    virt_addr_t remap_tbl;
    uint8_t     pml5_support;
    uint8_t     nx_support;
    
    pat_t pat;
}pagemgr_root_t;


#define PGMGR_LEVEL_TO_SHIFT(x) (PT_SHIFT + (((x) - 1) << 3) + ((x) - 1))

#define PGMGR_FILL_LEVEL(level_data, context, vbase, vlength, vmin_level)   \
                        (level_data)->ctx = (context);                      \
                        (level_data)->base = (vbase);                       \
                        (level_data)->length = (vlength);                   \
                        (level_data)->min_level = (vmin_level);             \
                        (level_data)->level = NULL;                         \
                        (level_data)->pos = vbase;                          \
                        (level_data)->current_level = (context)->max_level; \
                        (level_data)->error = 1;                            \
                        (level_data)->addr = (context)->pg_phys

typedef struct pgmgr_contig_find_t
{
    phys_addr_t base;
    phys_size_t pf_count;
    phys_size_t pf_req;
}pgmgr_contig_find_t;

typedef struct pgmgr_level_data
{
    pagemgr_ctx_t *ctx;
    virt_addr_t base;
    virt_addr_t *level;
    virt_size_t length;
    virt_size_t pos;
    phys_addr_t addr;
    uint8_t min_level;
    uint8_t current_level;
    uint8_t error;
}pgmgr_level_data_t;

#define PAGE_MASK_ADDRESS(x) (((x) & (~(ATTRIBUTE_MASK))))
#define PAGE_STRUCT_TEMP_MAP(x,y) pagemgr_temp_map(PAGE_MASK_ADDRESS((x)), (y))
#define PAGE_STRUCT_TEMP_UNMAP(x) pagemgr_temp_unmap(PAGE_MASK_ADDRESS((x)))
#define PGMGR_MIN_PAGE_TABLE_LEVEL (0x2)
#define PAGE_PATH_RESET(path)     ((path))->pml5_ix   = ~0; \
                                  ((path))->pml4_ix   = ~0; \
                                  ((path))->pdpt_ix   = ~0; \
                                  ((path))->pdt_ix    = ~0; \
                                  ((path))->virt_off  =  (virt_size_t)0;

#define PGMGR_NO_FRAMES (1 << 0)
#define PGMGR_TABLE_NOT_ALLOCATED (1 << 1)
#define PGMGR_TBL_CREATE_FAIL (1 << 2)
/* locals */
static pagemgr_root_t page_manager = {0};

static pfmgr_t       *pfmgr        = NULL;

static int         pagemgr_page_fault_handler(void *pv, isr_info_t *inf);
static int         pagemgr_per_cpu_invl_handler
(
    void *pv, 
    isr_info_t *inf
);


/* This piece code is the intermediate layer
 * between the physical memory manager and
 * the virtual memory manager.
 * It manages the page tables and handles page
 * faults.
 * 
 * In order for the page manager to work, we
 * will create a new page table.
 * 
 * In this page table, the last table (~2MB) will
 * be used to modify the page table by temporary 
 * mapping it to the needed physical region.
 * 
 * Also, the initial page table will also map the kernel 
 * image so that when we switch it, we won't get
 * a page fault which would result at that point in a
 * triple fault because the interrupt handler is not
 * yet installed
 */ 


int pagemgr_install_handler(void)
{
    isr_install(pagemgr_page_fault_handler, 
                &page_manager, 
                PLATFORM_PG_FAULT_VECTOR, 
                0);

    isr_install(pagemgr_per_cpu_invl_handler, 
                NULL, 
                PLATFORM_PG_INVALIDATE_VECTOR, 
                0);
}

uint8_t pagemgr_pml5_support(void)
{
    return(page_manager.pml5_support);
}

uint8_t pagemgr_nx_support(void)
{
    return(page_manager.nx_support);
}

void pagemgr_boot_temp_map_init(void)
{
    virt_addr_t page_table = 0;
    virt_addr_t *pde = 0;
    virt_addr_t *remap_page = 0;
    
    page_table = (virt_addr_t)&KERNEL_VMA + 
                 (virt_addr_t)&BOOT_PAGING;

    pde = (virt_addr_t*)((virt_addr_t)&BOOT_PAGING + 0x3000 + 511 * 8);

    remap_page = (virt_addr_t*)
                 (page_table + 0x4000 + 512 * 511 * 8);

    /* make the first page in the table to point to the table itself */
    *remap_page = *pde;

    page_manager.remap_tbl = BOOT_REMAP_TABLE_VADDR;   

}

static phys_size_t pagemgr_alloc_pf_cb
(
    phys_addr_t phys, 
    phys_size_t count, 
    void *pv
)
{
    phys_addr_t *pf = (phys_addr_t*)pv;
  
    if(*pf != 0)
        return(0);
    
    *pf = phys;

    return(1);
}

static phys_addr_t pagemgr_alloc_pf(phys_addr_t *pf)
{
    return(pfmgr->alloc(1, 0, pagemgr_alloc_pf_cb, pf));
}

static phys_addr_t pagemgr_free_pf_cb
(
    phys_addr_t *paddr, 
    phys_size_t *count, 
    void *pv
)
{
    *paddr = *(phys_addr_t*)pv;
    *count = 1;

    return(0);
}

static int pagemgr_free_pf
(
    phys_addr_t addr
)
{
    return(pfmgr->dealloc(pagemgr_free_pf_cb, &addr));
}


static int pagemgr_attr_translate(phys_addr_t *pte_mask, uint32_t attr)
{
    phys_addr_t pte = 0;

    if(attr & PGMGR_WRITABLE)
        pte |= PAGE_WRITABLE;
    
    if(attr & PGMGR_USER)
        pte |= PAGE_USER;
    
    if((attr & PGMGR_EXECUTABLE) && page_manager.nx_support)
        pte |= PAGE_EXECUTE_DISABLE;
    
    /* Translate caching attributes */
    /*
        * PAT PCD PWT PAT Entry
        *  0   0   0    PAT0 -> WB
        *  0   0   1    PAT1 -> WT
        *  0   1   0    PAT2 -> UC-
        *  0   1   1    PAT3 -> UC
        *  1   0   0    PAT4 -> WC
        *  1   0   1    PAT5 -> WT
        *  1   1   0    PAT6 -> UC-
        *  1   1   1    PAT7 -> UC
        * 
        *  just to have a picture here
        * 
        * pat.fields.pa0 = PAT_WRITE_BACK;
        * pat.fields.pa1 = PAT_WRITE_THROUGH;
        * pat.fields.pa2 = PAT_UNCACHED;
        * pat.fields.pa3 = PAT_UNCACHEABLE;
        * pat.fields.pa4 = PAT_WRITE_COMBINING;
        * pat.fields.pa5 = PAT_WRITE_PROTECT;
        * pat.fields.pa6 = PAT_UNCACHED;
        * pat.fields.pa7 = PAT_UNCACHEABLE;
        */

    /* PAT 0 */
    if(attr & PGMGR_WRITE_BACK)
    {
        pte &= ~(PAGE_WRITE_THROUGH | 
                 PAGE_CACHE_DISABLE | 
                 PAGE_PAT);
    }
    /* PAT 1 */
    else if(attr & PGMGR_WRITE_THROUGH)
    {
        pte &= ~(PAGE_CACHE_DISABLE | 
                 PAGE_PAT);

        pte |= PAGE_WRITE_THROUGH;
    }
    /* PAT 2 */
    else if(attr & PGMGR_UNCACHEABLE)
    {
        pte &= ~(PAGE_WRITE_THROUGH |
                 PAGE_PAT);
        pte |= PAGE_CACHE_DISABLE;
    }
    /* PAT 3 */
    else if(attr & PGMGR_STRONG_UNCACHED)
    {
        pte |= (PAGE_WRITE_THROUGH | 
                PAGE_CACHE_DISABLE);
        pte &= ~PAGE_PAT;
    }
    /* PAT4 */
    else if(attr & PGMGR_WRITE_COMBINE)
    {
        pte |= PAGE_PAT;
        pte &= ~(PAGE_CACHE_DISABLE | 
                 PAGE_WRITE_THROUGH);
    }
    else if(attr & PGMGR_WRITE_PROTECT)
    {
        pte |= (PAGE_WRITE_THROUGH | PAGE_PAT);
        pte &= ~PAGE_CACHE_DISABLE;
    }
    /* By default do write-back */
    else
    {
        pte &= ~(PAGE_WRITE_THROUGH | 
                 PAGE_CACHE_DISABLE | 
                 PAGE_PAT);
    }

    *pte_mask = pte;

    return(0);
}

static phys_size_t pagemgr_ensure_levels_cb
(
    phys_addr_t base,
    phys_size_t pf_count,
    void *pv
    
)
{
    pgmgr_level_data_t *ld  = NULL;
    pagemgr_ctx_t      *ctx       = NULL;
    virt_addr_t        next_pos   = 0;
    uint16_t           entry      = 0;
    uint8_t            shift      = 0;
    phys_size_t        used_bytes = 0;
    phys_size_t        total_bytes   = 0;
    virt_addr_t        vend =      0;

    ld = pv;
    ctx = ld->ctx;

    total_bytes = pf_count << PAGE_SIZE_SHIFT;

   /* kprintf("MAX_LEVEL %d\n",ctx->max_level);*/

    if(!ld->addr)
    {
        kprintf("THIS IS AN ERROR\n");
        while(1);
    }

    if(!ld->current_level)
    {
        kprintf("CURRENT_LEVEL_IS_ZERO\n");
        ld->current_level = ctx->max_level;
    }
    /* Make sure that the min_level is at least 2 
     * A level of 2 means that it will only create the page tables but will
     * not try to allocate page frames
     */
    if((ld->min_level < PGMGR_MIN_PAGE_TABLE_LEVEL) || 
       (ld->min_level >= ctx->max_level))
    {
        ld->min_level = PGMGR_MIN_PAGE_TABLE_LEVEL;
    }

    vend = ld->base + (ld->length - 1);
    next_pos = ld->pos;
    
    ld->error = PGMGR_TBL_CREATE_FAIL;

    while((ld->pos    < vend) && 
          (used_bytes < total_bytes))
    {
        ld->level = (virt_addr_t*)
                          pagemgr_temp_map(ld->addr, 
                                           ld->current_level);

        shift = PGMGR_LEVEL_TO_SHIFT(ld->current_level);
        entry = (ld->pos >> shift) & 0x1FF;

#if 1
        kprintf("Current_level %d -> %x - PHYS %x " \
                "PHYS_SAVED %x ENTRY %d POS %x Increment %x\n",
                ld->current_level, 
                ld->level,
                ld->level[entry],
                ld->addr,
                entry, 
                ld->pos,
                (virt_size_t)1 << shift);
#endif
        /* Check if we have an address so that we can dive in
         * if we don't have an address, then save it 
         */
        if(!PAGE_MASK_ADDRESS(ld->level[entry]))
        {
            ld->level[entry] = base + used_bytes;
            ld->level[entry] |= (PAGE_PRESENT | PAGE_WRITABLE);
            used_bytes += PAGE_SIZE;
        }

        /* If we are not at the bottom level, go DEEEPAH */

        if(ld->current_level > ld->min_level)
        {  
            ld->current_level--;
            ld->addr = ld->level[entry];
            continue;
        }

        next_pos += (virt_size_t)1 << shift;
 
        /* In case we overflow, detect this and break out */
        if(next_pos < ld->pos)
        {
            kprintf("OVERFLOWED\n");
            ld->error = 0;
            break;
        }

        if(((next_pos >> shift) & 0x1FF) < entry)
        {
            /* Calcuate how much we need to go up */
            while(ld->current_level < ctx->max_level)
            {
                shift = PGMGR_LEVEL_TO_SHIFT(ld->current_level);
                
                if(((next_pos >> shift) & 0x1FF) > 
                    ((ld->pos >> shift) & 0x1FF))
                {
                    break;
                }

                ld->current_level ++;
            }

            if(ld->current_level  < ctx->max_level)
            {
               /* the upper levels should be the same so just calculate
                * the address from the base remapping table 
                */
                ld->level = (virt_addr_t*) (page_manager.remap_tbl + 
                                        (ld->current_level  << PAGE_SIZE_SHIFT));
                
                entry = (next_pos >> shift) & 0x1FF;
                
                /* check if the upper level is allocated */
                if(!PAGE_MASK_ADDRESS(ld->level[entry]))
                {
                    ld->level[entry] = base + used_bytes;
                    ld->level[entry] |= (PAGE_PRESENT | PAGE_WRITABLE);
                    used_bytes += PAGE_SIZE;
                }
                
                /* Store the level and go down again */
                ld->addr = ld->level[entry];
                ld->current_level--;
            }
            else
            {
                /* For the topmost level, use the 
                 * page base address
                 */
                ld->addr = ctx->pg_phys;
            }
        }

        ld->pos = next_pos;
    }
   
    if(ld->pos == next_pos)
        ld->error = 0;

    return(used_bytes >> PAGE_SIZE_SHIFT);
}



static phys_size_t pagemgr_fill_tables_cb
(
    phys_addr_t base,
    phys_size_t pf_count,
    void *pv
    
)
{
    pgmgr_level_data_t *ld  = NULL;
    pagemgr_ctx_t      *ctx       = NULL;
    virt_addr_t        next_pos   = 0;
    uint16_t           entry      = 0;
    uint8_t            shift      = 0;
    phys_size_t        used_bytes = 0;
    phys_size_t        total_bytes   = 0;
    virt_addr_t        vend =      0;

    ld = pv;
    ctx = ld->ctx;

    total_bytes = pf_count << PAGE_SIZE_SHIFT;
    kprintf("%s\n",__FUNCTION__);
   /* kprintf("MAX_LEVEL %d\n",ctx->max_level);*/

    if(!ld->addr)
    {
        kprintf("THIS IS AN ERROR\n");
        while(1);
    }

    if(!ld->current_level)
    {
        kprintf("CURRENT_LEVEL_IS_ZERO\n");
        ld->current_level = ctx->max_level;
    }
    /* Make sure that the min_level is at least 2 
     * A level of 2 means that it will only create the page tables but will
     * not try to allocate page frames
     */

    if((ld->min_level < 1) || 
       (ld->min_level >= ctx->max_level))
    {
        ld->min_level = 1;
    }

    vend = ld->base + (ld->length - 1);
    next_pos = ld->pos;

    while((ld->pos    < vend) && 
          (used_bytes < total_bytes))
    {
        ld->level = (virt_addr_t*)pagemgr_temp_map(ld->addr, 
                                           ld->current_level);

        shift = PGMGR_LEVEL_TO_SHIFT(ld->current_level);
        entry = (ld->pos >> shift) & 0x1FF;

#if 0
        kprintf("Current_level %d -> %x - PHYS %x " \
                "PHYS_SAVED %x ENTRY %d POS %x Increment %x\n",
                ld->current_level, 
                ld->level,
                ld->level[entry],
                ld->addr,
                entry, 
                ld->pos,
                (virt_size_t)1 << shift);
#endif
        
        /* If we didn't reach the bottom level, check if the page table
         * is allocated. If it's not, mark it as an error and bail out
         * Also check if the PAGE_TABLE_SIZE bit is set so a page bigger
         * than 4KB would not trigger an error
         */
        if((ld->current_level > PGMGR_MIN_PAGE_TABLE_LEVEL) && 
            !PAGE_MASK_ADDRESS(ld->level[entry])             && 
            !(ld->level[entry] & PAGE_TABLE_SIZE))
        {
            ld->error = PGMGR_TABLE_NOT_ALLOCATED;
            kprintf("NOT_ALLOCATED\n");
            return(0);
        }

        /* If we haven't reach the minimum level
         * and the PAGE_TABLE_SIZE flag is not set,
         * then go a bit lower
         */

        if((ld->current_level > ld->min_level) &&
            !(ld->level[entry] & PAGE_TABLE_SIZE))
        {  
            ld->current_level--;
            ld->addr = ld->level[entry];
            continue;
        }


        ld->level[entry] = base + used_bytes;
        used_bytes += (virt_size_t)1 << shift;

        /* Calculate the next position */
        next_pos += (virt_size_t)1 << shift;

        /* In case we overflow, detect this and break out 
         * This is a special case when we are dealing with the last
         * bits of the address space
         */
        if(next_pos < ld->pos)
        {
            kprintf("OVERFLOWED\n");
            ld->error = 0;
            break;
        }

        /* Check if we need to go upper */
        if(((next_pos >> shift) & 0x1FF) < entry)
        {
            /* Calcuate how much we need to go up */
            while(ld->current_level < ctx->max_level)
            {
                shift = PGMGR_LEVEL_TO_SHIFT(ld->current_level);
                
                if((((next_pos >> shift) & 0x1FF) > 
                    ((ld->pos >> shift) & 0x1FF)))
                {
                    break;
                }

                ld->current_level ++;
            }

            if(ld->current_level  < ctx->max_level)
            {
               /* the upper levels should be the same so just calculate
                * the address from the base remapping table 
                */
                ld->level = (virt_addr_t*) (page_manager.remap_tbl + 
                                        (ld->current_level  << PAGE_SIZE_SHIFT));
               
                entry = (next_pos >> shift) & 0x1FF;
               
                if((ld->current_level >= PGMGR_MIN_PAGE_TABLE_LEVEL) && 
                    !PAGE_MASK_ADDRESS(ld->level[entry]) && 
                    !(ld->level[entry] & PAGE_TABLE_SIZE))
                {
                    ld->error = PGMGR_TABLE_NOT_ALLOCATED;
                    return(0);
                }

                /* Store the level and go down again */
                ld->addr = ld->level[entry];
                ld->current_level--;
            }
            else
            {
                /* For the topmost level, use the 
                 * page base address
                 */
                kprintf("GO_TO_ROOT\n");
                ld->addr = ctx->pg_phys;
            }
        }

        ld->pos = next_pos;
    }
   
    if(ld->pos == next_pos)
        ld->error = 0;

    return(used_bytes >> PAGE_SIZE_SHIFT);
}


static int pgmgr_setup_remap_table(pagemgr_ctx_t *ctx)
{
    pgmgr_level_data_t lvl_dat;
    uint8_t current_level = 0;
    virt_addr_t *level    = NULL;
    phys_addr_t addr      = 0;
    uint16_t entry        = 0;
    uint8_t shift         = 0;
    int status            = 0;

    PGMGR_FILL_LEVEL(&lvl_dat, ctx, REMAP_TABLE_VADDR, REMAP_TABLE_SIZE, 2);

    /* Allocate pages */
    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pagemgr_ensure_levels_cb, 
                          &lvl_dat);

    if(status || lvl_dat.error)
    {
        kprintf("Failed to allocate the required levels\n");
        while(1);
    }

    addr = ctx->pg_phys;

    for(current_level = ctx->max_level; current_level > 0; current_level--)
    {
        shift = PGMGR_LEVEL_TO_SHIFT(current_level);
        entry = (REMAP_TABLE_VADDR >> shift) & 0x1FF;

        level = (virt_addr_t*)pagemgr_temp_map(addr, current_level);
        kprintf("ADDR %x - LEVEL %x - %x\n", addr, level, level[entry]);

        /* If we reach the bottom level, map the first page from the level 
         * to the to the table from it belongs 
         */

        if(current_level < PGMGR_MIN_PAGE_TABLE_LEVEL)
        {
            level[entry] = addr;
            break;
        }
        addr= level[entry];
    }

    return(0);
}

static phys_size_t pgmgr_contig_pf_cb
(
    phys_addr_t base,
    phys_size_t pf_count,
    void *pv
)
{
    pgmgr_contig_find_t *pcf = NULL;

    pcf = pv;

    if(pcf->pf_req == pf_count)
    {
        pcf->base = base;
        pcf->pf_count = pf_count;
        return(pf_count);
    }
    
    return(0);
}

static int pgmgr_contig_pf
(
    phys_addr_t *base, 
    phys_size_t *count,
    phys_size_t req_size
)
{
    int status = 0;
    pgmgr_contig_find_t pcf;

    pcf.pf_req = req_size >> PAGE_SIZE_SHIFT;

    status = pfmgr->alloc(pcf.pf_req, 
                          ALLOC_CONTIG, 
                          pgmgr_contig_pf_cb, 
                          &pcf);

    *base = pcf.base;
    *count = pcf.pf_count;

    return(status);
}

static int pgmgr_map
(
    pagemgr_ctx_t *ctx,
    virt_addr_t    virt,
    virt_size_t    length,
    phys_addr_t    phys, 
    uint32_t       attr
)
{
    pgmgr_level_data_t ld;
    virt_addr_t pos = 0;
    virt_addr_t next_pos = 0;
    virt_addr_t vlimit = 0;
    uint8_t current_level = 0;
    uint16_t entry = 0;
    int status = 0;

    vlimit = virt + (length - 1);

    /* Create the tables */
    PGMGR_FILL_LEVEL(&ld, ctx, virt, length, 2);

    status = pfmgr->alloc(0, ALLOC_CB_STOP, pagemgr_ensure_levels_cb, &ld);

 PGMGR_FILL_LEVEL(&ld, ctx, virt, length, 2);

    status = pfmgr->alloc(0, ALLOC_CB_STOP, pagemgr_ensure_levels_cb, &ld);

    if(status || ld.error)
        return(-1);

    PGMGR_FILL_LEVEL(&ld, ctx, virt, length, 1);

   /* pagemgr_fill_tables_cb(phys, length >> PAGE_SIZE_SHIFT, &ld);*/

    return(0);
}


static pgmgr_init_kernel_pgtable(pagemgr_ctx_t *ctx)
{
    pgmgr_level_data_t lvl_dat;
    uint8_t current_level = 0;
    virt_addr_t *level    = NULL;
    phys_addr_t addr      = 0;
    uint16_t entry        = 0;
    uint8_t shift         = 0;
    int status            = 0;

    PGMGR_FILL_LEVEL(&lvl_dat, ctx, 
                    _KERNEL_VMA, 
                    _KERNEL_VMA_END - _KERNEL_VMA, 
                    2);

    status = pfmgr->alloc(0, ALLOC_CB_STOP, pagemgr_ensure_levels_cb, &lvl_dat);

    if(status || lvl_dat.error)
    {
        kprintf("Failed to allocate the required levels\n");
        while(1);
    }

    addr = ctx->pg_phys;

    for(current_level = ctx->max_level; current_level > 0; current_level--)
    {
        shift = PGMGR_LEVEL_TO_SHIFT(current_level);
        entry = (REMAP_TABLE_VADDR >> shift) & 0x1FF;

        level = (virt_addr_t*)pagemgr_temp_map(addr, current_level);
        kprintf("ADDR %x - LEVEL %x - %x\n", addr, level, level[entry]);

        /* If we reach the bottom level, map the first page from the level 
         * to the to the table from it belongs 
         */
        if(current_level == 1)
        {
            level[entry] = addr;
            break;
        }
        addr= level[entry];
    }

    return(0);
}


static int pgmgr_init_pt(pagemgr_ctx_t *ctx)
{
    
}

static int pgmgr_check_nx(void)
{
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    eax = 0x80000001;

    __cpuid(&eax, &ebx, &ecx, &edx);

    return(!!(edx & (1 << 20)));
}

static void pgmgr_enable_nx(void)
{
    uint64_t reg_val = 0;

    reg_val = __rdmsr(0xC0000080);

    reg_val |= (1 << 11); /* enable NX */

    __wrmsr(0xC0000080, reg_val);
}

static int pgemgr_pml5_is_enabled(void)
{
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    int      enabled = 0;
    uint64_t cr4     = 0;
    eax = 0x7;

    /* Check if PML5 is available */
    
    __cpuid(&eax, &ebx, &ecx, &edx);
    cr4 = __read_cr4();

    if((ecx & (1 << 16)) && ((cr4 & (1 << 12))))
        enabled = 1;

    /* Check if we have enabled it in the CPU */

    return(enabled);
}
/* This should be called only once */

int pagemgr_init(pagemgr_ctx_t *ctx)
{
    pat_t *pat = NULL;
    pgmgr_level_data_t lvl_dat;

    memset(&page_manager.pat, 0, sizeof(pat_t));

    pat = &page_manager.pat;

    pfmgr = pfmgr_get();

    kprintf("Initializing Page Manager\n");

    spinlock_init(&ctx->lock);

    kprintf("Setting page manager\n");

    page_manager.pml5_support = pgemgr_pml5_is_enabled();
    page_manager.nx_support   = pgmgr_check_nx();


    kprintf("PML5 %d\n", page_manager.pml5_support);

    if(page_manager.pml5_support)
        ctx->max_level = 5;
    else
        ctx->max_level = 4;

    pagemgr_alloc_pf(&ctx->pg_phys);
 
 #if 0
    for(int i = 0; i <1; i++)
    {
        kprintf("ITER %d\n",i);
    PGMGR_FILL_LEVEL(&lvl_dat,
                     ctx,
                     0xffff800000000000, 
                     1024ull*1024ull*1024ull,
                     2);

    pfmgr->alloc(0, ALLOC_CB_STOP, pagemgr_ensure_levels, &lvl_dat);
    }
#endif
    pgmgr_setup_remap_table(ctx);
    
   pgmgr_map(ctx, 0xffff800000000000, 0x400000000,0x2000000 , 0);

 /*pgmgr_map(ctx, 0xffff800000000000, 0x2000000, 0x2000000, 0);*/
   #if 0 
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
pagemgr_ensure_levels(ctx,0xffff800000000000, 0x400000000, 2);
#endif
kprintf("OK\n");
    while(1);

#if 0
    ctx->page_phys_base = pagemgr_boot_alloc_pf();

    kprintf("PHYS_BASE 0x%x\n",ctx->page_phys_base);

    if(!ctx->page_phys_base)
        return(-1);
#endif


    if(pagemgr_build_init_pagetable(ctx) == -1)
        return(-1);

    /* If we support NX, enable it */
    if(page_manager.nx_support)
        pgmgr_enable_nx();


    /* Prepare PAT */

    pat->fields.pa0 = PAT_WRITE_BACK;
    pat->fields.pa1 = PAT_WRITE_THROUGH;
    pat->fields.pa2 = PAT_UNCACHED;
    pat->fields.pa3 = PAT_UNCACHEABLE;
    pat->fields.pa4 = PAT_WRITE_COMBINING;
    pat->fields.pa5 = PAT_WRITE_PROTECTED;
    pat->fields.pa6 = PAT_UNCACHED;
    pat->fields.pa7 = PAT_UNCACHEABLE;

        
    /* Flush cached changes */ 

    __wbinvd();
    
    /* write pat */
    
    __wrmsr(PAT_MSR, pat->pat);

    /* switch to the new page table */
    
    __write_cr3(ctx->pg_phys);
    
    /* Flush again */
    __wbinvd();
    
    return(0);
}

virt_addr_t pagemgr_temp_map(phys_addr_t phys, uint16_t ix)
{
    virt_addr_t *remap_tbl = (virt_addr_t*)page_manager.remap_tbl;
    virt_addr_t remap_value = 0;
    pte_bits_t pte;

    memset(&pte, 0, sizeof(pte_bits_t));

    /* Ix is not allowd to be 0 - root of the remapping table 
     * or above 511 - max table 
     */

    if(ix > 511 || ix == 0)
    {
        kprintf("DIED @ %s %d\n",__FUNCTION__,__LINE__);
        return(-1);
    }

    phys = PAGE_MASK_ADDRESS(phys);
   
    if(phys % PAGE_SIZE)
    {
        kprintf("DIED @ %s %d\n",__FUNCTION__,__LINE__);
        return(0);
    }

    pte.bits = phys;
    pte.fields.read_write    = 1;
    pte.fields.present       = 1;

    remap_tbl[ix] = pte.bits;

    remap_value = page_manager.remap_tbl + (PAGE_SIZE * ix);

    __invlpg(remap_value);

    return(remap_value);
}



 int pagemgr_temp_unmap(virt_addr_t vaddr)
{
    uint16_t ix = 0;
    virt_addr_t *remap_tbl = (virt_addr_t*)REMAP_TABLE_VADDR;

    if(vaddr % PAGE_SIZE || vaddr <= REMAP_TABLE_VADDR)
        return(-1);

    ix = (vaddr - REMAP_TABLE_VADDR) / PAGE_SIZE;

    /* remove address */
    remap_tbl[ix] = 0;

    /* invalidate entry */
    __invlpg(vaddr);

    return(0);
}

static inline void pagemgr_invalidate(virt_addr_t addr)
{
    /* For this CPU */
    __invlpg(addr);

    /* For other CPUs we should at least send IPI */
    cpu_issue_ipi(IPI_DEST_ALL_NO_SELF, 0, IPI_INVLPG);

}

static inline void pagemgr_invalidate_all(void)
{
    __write_cr3(__read_cr3());
    cpu_issue_ipi(IPI_DEST_ALL_NO_SELF, 0, IPI_INVLPG);
}

virt_addr_t pagemgr_map
(
    pagemgr_ctx_t *ctx,
    virt_addr_t    virt, 
    phys_addr_t    phys, 
    virt_size_t    length, 
    uint32_t       attr
)
{
    #if 0
    pagemgr_path_t path;
    int            ret       = 0;
    int            int_status = 0;
    phys_size_t    pg_frames = 0;

    memset(&path, 0, sizeof(pagemgr_path_t));

    path.virt     = virt;
    path.req_len  = length;
    path.attr     = attr;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);

    spinlock_lock_int(&ctx->lock, &int_status);

    if(pagemgr_build_page_path(&path) < 0)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(0);
    }
    
    PAGE_PATH_RESET(&path);

    pg_frames     = length / PAGE_SIZE;

    pagemgr_alloc_or_map_cb(phys, pg_frames, &path);
    pagemgr_invalidate_all();
    spinlock_unlock_int(&ctx->lock, int_status);
    
    return(virt);
    #endif
}

virt_addr_t pagemgr_alloc
(
    pagemgr_ctx_t *ctx,
    virt_addr_t    virt, 
    virt_size_t    length, 
    uint32_t       attr
)
{
    #if 0
    pagemgr_path_t path;
    phys_addr_t    pg_frames  = 0;
    int            ret        = 0;
    int            int_status = 0;
 
    memset(&path, 0, sizeof(pagemgr_path_t));
 
    path.virt     = virt;
    path.req_len  = length;
    path.attr     = attr;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    
    spinlock_lock_int(&ctx->lock, &int_status);
    
    ret = pagemgr_build_page_path(&path);

    if(ret < 0)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(0);
    }   

    PAGE_PATH_RESET(&path);

    pg_frames = length / PAGE_SIZE;
    
    ret = pfmgr->alloc(pg_frames, 0, 
                       (alloc_cb)pagemgr_alloc_or_map_cb, 
                       (void*)&path);    
    
    if(ret < 0)
    {
        kprintf("NO MORE BMP\n");
        spinlock_unlock_int(&ctx->lock, int_status);
        pagemgr_free(ctx, virt, path.virt_off);
        pagemgr_invalidate_all();
        kprintf("RELEASED\n");
        return(0);
    }
    pagemgr_invalidate_all();
    spinlock_unlock_int(&ctx->lock, int_status);

    return(virt);
    #endif


    return (0);
}

int pagemgr_attr_change
(
    pagemgr_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len, 
    uint32_t attr
)
{
    return(0);
}

int pagemgr_free
(
    pagemgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    return(0);
}

int pagemgr_unmap
(
    pagemgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    return(0);
}


static int pagemgr_page_fault_handler(void *pv, isr_info_t *inf)
{

    interrupt_frame_t *int_frame = 0;
    virt_addr_t fault_address = 0;
    virt_addr_t error_code = *(virt_addr_t*)(inf->iframe - sizeof(uint64_t));

    fault_address = __read_cr2();
    int_frame = (interrupt_frame_t*)inf->iframe;

    kprintf("ADDRESS 0x%x ERROR 0x%x IP 0x%x SS 0x%x RFLAGS 0x%x\n",
            fault_address,  \
            error_code, \
            int_frame->rip,
            int_frame->ss,
            int_frame->rflags);



    while(1);

    return(0);
}

static int pagemgr_per_cpu_invl_handler
(
    void *pv, 
    isr_info_t *inf
)
{
    int status = 0;
    
    
   // kprintf("INVALIDATING on CPU %d\n", cpu_id_get());

    __write_cr3(__read_cr3());
    return(0);
}

int pagemgr_per_cpu_init(void)
{
    virt_addr_t cr0 = 0;
    virt_addr_t cr3 = 0;

    /* enable write protect*/
    cr0 = __read_cr0();

    cr0 |= (1 << 16);
    cr0 &= ~((1 << 29) | (1 << 30));

    __write_cr0(cr0);

    __wbinvd();
    __wrmsr(PAT_MSR, page_manager.pat.pat);
    
    cr3 = __read_cr3();

    __write_cr3(cr3);
    
    __wbinvd();

    return(0);
}