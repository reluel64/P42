/* Paging management 
 * This file contains code that would 
 * allow us to manage paging 
 * structures
 */


#include <stdint.h>
#include <paging.h>
#include <utils.h>
#include <pgmgr.h>
#include <isr.h>
#include <spinlock.h>
#include <vm.h>
#include <platform.h>
#include <intc.h>
#include <pfmgr.h>

typedef struct pgmgr_t
{
    virt_addr_t remap_tbl;
    uint8_t     pml5_support;
    uint8_t     nx_support;
    
    pat_t pat;
    isr_t fault_isr;
    isr_t inv_isr;
}pgmgr_t;

#define PGMGR_CHANGE_ATTRIBUTES (1 << 0)
#define PGMGR_ASSIGN_ADDRESS    (1 << 1)
#define PGMGR_CREATE_LEVELS     (1 << 2)
#define PGMGR_RELEASE_ADDRESS   (1 << 3)
#define PGMGR_RELEASE_LEVELS    (1 << 4)



#define PGMGR_LEVEL_TO_SHIFT(x) (PT_SHIFT + (((x) - 1) << 3) + ((x) - 1))
#define PGMGR_ENTRIES_PER_LEVEL (512)
#define PGMGR_FILL_LEVEL(ld, context, _base,                        \
                        _length, _min_level, _attr, _flags)         \
                        (ld)->ctx = (context);                      \
                        (ld)->base = (_base);                       \
                        (ld)->level = NULL;                         \
                        (ld)->length = (_length);                   \
                        (ld)->pos = (_base);                        \
                        (ld)->addr = (context)->pg_phys;            \
                        (ld)->min_level = (_min_level);             \
                        (ld)->curr_level = (context)->max_level;    \
                        (ld)->error = 1;                            \
                        (ld)->do_map = 1;                           \
                        (ld)->attr_mask = (_attr);                  \
                        (ld)->flags = (_flags)                      


typedef struct pgmgr_contig_find_t
{
    phys_addr_t base;
    phys_size_t pf_count;
    phys_size_t pf_req;
}pgmgr_contig_find_t;

typedef struct pgmgr_level_data_t
{
    pgmgr_ctx_t *ctx;
    virt_addr_t base;
    virt_addr_t *level;
    virt_size_t length;
    virt_size_t pos;
    phys_addr_t addr;
    uint8_t min_level;
    uint8_t curr_level;
    uint8_t error;
    uint8_t do_map;
    uint8_t clear;
    uint8_t flags;
    phys_size_t attr_mask;
}pgmgr_level_data_t;

#define PAGE_MASK_ADDRESS(x) (((x) & (~(ATTRIBUTE_MASK))))
#define PGMGR_MIN_PAGE_TABLE_LEVEL (0x2)
#define PGMGR_LEVEL_TO_STEP(lvl) (((virt_size_t)1 << PGMGR_LEVEL_TO_SHIFT((lvl))))
#define PAGE_PATH_RESET(path)     ((path))->pml5_ix   = ~0; \
                                  ((path))->pml4_ix   = ~0; \
                                  ((path))->pdpt_ix   = ~0; \
                                  ((path))->pdt_ix    = ~0; \
                                  ((path))->virt_off  =  (virt_size_t)0;

#define PGMGR_ERR_OK                  (0)
#define PGMGR_ERR_NO_FRAMES           (1 << 0)
#define PGMGR_ERR_TABLE_NOT_ALLOCATED (1 << 1)
#define PGMGR_ERR_TBL_CREATE_FAIL     (1 << 2)

/* locals */
static pgmgr_t pgmgr;
static pfmgr_t *pfmgr        = NULL;

static int         pgmgr_page_fault_handler(void *pv, isr_info_t *inf);
static int         pgmgr_per_cpu_invl_handler
(
    void *pv, 
    isr_info_t *inf
);

static virt_addr_t _pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
);

static int _pgmgr_temp_unmap
(
    virt_addr_t vaddr
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


int pgmgr_install_handler(void)
{
    isr_install(pgmgr_page_fault_handler, 
                &pgmgr, 
                PLATFORM_PG_FAULT_VECTOR, 
                0,
                &pgmgr.fault_isr);

    isr_install(pgmgr_per_cpu_invl_handler, 
                NULL, 
                PLATFORM_PG_INVALIDATE_VECTOR, 
                0,
                &pgmgr.inv_isr);

    return(0);

}

uint8_t pgmgr_pml5_support(void)
{
    return(pgmgr.pml5_support);
}

uint8_t pgmgr_nx_support(void)
{
    return(pgmgr.nx_support);
}

void pgmgr_boot_temp_map_init(void)
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

    pgmgr.remap_tbl = BOOT_REMAP_TABLE_VADDR;   

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
    uint64_t cr4     = 0;
    int      enabled = 0;

    eax = 0x7;

    /* Check if PML5 is available */
    
    __cpuid(&eax, &ebx, &ecx, &edx);
    cr4 = __read_cr4();

    if((ecx & (1 << 16)) && ((cr4 & (1 << 12))))
        enabled = 1;

    /* Check if we have enabled it in the CPU */

    return(enabled);
}

static phys_size_t pgmgr_alloc_pf_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    phys_addr_t *pf = (phys_addr_t*)pv;
  
    if(*pf != 0)
        return(0);
    
    *pf = cb_dat->phys_base;
    cb_dat->used_bytes = PAGE_SIZE;

    return(1);
}

static phys_addr_t pgmgr_alloc_pf(phys_addr_t *pf)
{
    return(pfmgr->alloc(1, 0, pgmgr_alloc_pf_cb, pf));
}

static int pgmgr_free_pf_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    cb_dat->phys_base = *(phys_addr_t*)pv;
    cb_dat->used_bytes += PAGE_SIZE;

    return(0);
}

static int pgmgr_free_pf
(
    phys_addr_t addr
)
{
    return(pfmgr->dealloc(pgmgr_free_pf_cb, &addr));
}


static int pgmgr_attr_translate
(
    phys_addr_t *pte_mask, 
    uint32_t attr
)
{
    phys_addr_t pte = 0;

    if(attr & PGMGR_WRITABLE)
        pte |= PAGE_WRITABLE;
    
    if(attr & PGMGR_USER)
        pte |= PAGE_USER;
    
    if(!(attr & PGMGR_EXECUTABLE) && pgmgr.nx_support)
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

static void pgmgr_clear_pt(pgmgr_ctx_t *ctx, phys_addr_t addr)
{
    virt_addr_t vaddr = 0;

    vaddr = _pgmgr_temp_map(addr, ctx->max_level + 1);

    if(vaddr != 0)
    {
        memset((void*)vaddr, 0, PAGE_SIZE);
        _pgmgr_temp_unmap(vaddr);
        
    }
}

static int pgmgr_level_is_empty
(
    virt_addr_t *level
)
{
    uint16_t i = 0;
    
    while(i < PGMGR_ENTRIES_PER_LEVEL)
    {
        if(level[i] != 0)
            break;
        
        i++;
    }

    return(i == PGMGR_ENTRIES_PER_LEVEL);
}

static phys_size_t pgmgr_alloc_levels_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    pgmgr_level_data_t *ld         = NULL;
    pgmgr_ctx_t      *ctx        = NULL;
    virt_addr_t        next_pos    = 0;
    uint16_t           entry       = 0;
    uint8_t            shift       = 0;
    virt_addr_t        vend        = 0;

    ld          = pv;
    ctx         = ld->ctx;
    next_pos    = ld->pos;
    vend        = ld->base + ld->length - 1;
    
   /* Fix for a nasty issue where a level would be skipped */
    if(ld->min_level > 1)
    {   
        vend = ALIGN_UP(vend, PGMGR_LEVEL_TO_STEP(ld->min_level));
        vend -= 1;
    }
    
    /* We're done here */
    if(vend < ld->pos)
    {
       
        ld->error = PGMGR_ERR_OK;
        return(0);
    }

    ld->error = PGMGR_ERR_TBL_CREATE_FAIL;

    if(!ld->curr_level || !ld->addr)
    {
        kprintf("CURRENT_LEVEL_IS_ZERO\n");
        ld->curr_level = ctx->max_level;
        ld->addr       = ctx->pg_phys;
        ld->do_map     = 1;
    }
   
    while((ld->pos < vend)                         && 
          (cb_dat->used_bytes < cb_dat->avail_bytes))
    {

        if(ld->do_map || ld->level == NULL)
        {
            ld->level = (virt_addr_t*) _pgmgr_temp_map(ld->addr, 
                                                       ld->curr_level
                                                      );
            ld->do_map = 0;
        }

        shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);
        entry = (ld->pos >> shift) & 0x1FF;
        
#if 0
        kprintf("Current_level %d -> %x - PHYS %x " \
                "PHYS_SAVED %x ENTRY %d POS %x Increment %x\n",
                ld->curr_level, 
                ld->level,
                ld->level[entry],
                ld->addr,
                entry, 
                ld->pos,
                (virt_size_t)1 << shift);
#endif

        /* If we haven't reached the target level
         * then we have to do down by obtaining the
         * address for the lower page table entry 
         */

        if(ld->curr_level > ld->min_level)
        {
            /* check if the page table entry is present */
            if(~ld->level[entry] & PAGE_PRESENT)
            {
                ld->level[entry] = cb_dat->phys_base  + 
                                   cb_dat->used_bytes + 
                                   PAGE_PRESENT       + 
                                   PAGE_WRITABLE;
                
                cb_dat->used_bytes += PAGE_SIZE;
                /* Make sure that the underlying table is clean */
                pgmgr_clear_pt(ctx, ld->level[entry]);
            }

            ld->addr = ld->level[entry];
            ld->curr_level--;
            ld->do_map = 1;

            continue;
        }

        if(ld->flags & PGMGR_CREATE_LEVELS)
        {
            if(~ld->level[entry] & PAGE_PRESENT)
            {
                ld->level[entry] = (cb_dat->phys_base + 
                                    cb_dat->used_bytes) | 
                                   PAGE_WRITABLE        | 
                                   PAGE_PRESENT;
             
                cb_dat->used_bytes += PAGE_SIZE;
                pgmgr_clear_pt(ctx, ld->level[entry]);
            }
        }
        else if(ld->flags & PGMGR_ASSIGN_ADDRESS)
        {
            if(~ld->level[entry] & PAGE_PRESENT)
            {
                ld->level[entry] = (cb_dat->phys_base + 
                                    cb_dat->used_bytes) | 
                                   ld->attr_mask        | 
                                   PAGE_PRESENT;
             
                cb_dat->used_bytes += (virt_size_t)1 << shift;
            }
        }
        else if(ld->flags & PGMGR_CHANGE_ATTRIBUTES)
        {
            if(ld->level[entry] & PAGE_PRESENT)
            {
                ld->level[entry] = PAGE_MASK_ADDRESS(ld->level[entry]) | 
                                   ld->attr_mask                      |
                                   PAGE_PRESENT;
                 cb_dat->used_bytes += (virt_size_t)1 << shift;
            }
            else
            {
                ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                return(-1);
            }
        }

        next_pos  += ((virt_size_t) 1 << shift);
        
        if(next_pos < ld->pos)
        {
            ld->error = PGMGR_ERR_OK;
            return(0);
        }

        if(((next_pos >> shift) & 0x1FF) < entry)
        {
            /* Calcuate how much we need to go up */
            while(ld->curr_level < ctx->max_level)
            {
                ld->curr_level ++;
                shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);

                if(((next_pos >> shift) & 0x1FF) >
                    ((ld->pos >> shift) & 0x1FF))
                {
                    break;
                }
            }

            /* We will map again */
            ld->do_map = 1;

            if(ld->curr_level  < ctx->max_level)
            {
               /* the upper levels should be the same so just calculate
                * the address from the base remapping table 
                */
                ld->level = (virt_addr_t*) (pgmgr.remap_tbl + 
                            (ld->curr_level  << PAGE_SIZE_SHIFT));
                
                entry = (next_pos >> shift) & 0x1FF;
                
                /* 
                 * If we are ensuring levels, check if we have an 
                 * address so that we can dive in
                 * if we don't have an address, then save it.
                 */
                if(ld->flags & PGMGR_CREATE_LEVELS)
                {
                    if(~ld->level[entry] & PAGE_PRESENT)
                    {
                        /* check if the upper level is allocated */
                        ld->level[entry] = cb_dat->phys_base  + 
                                           cb_dat->used_bytes + 
                                           PAGE_PRESENT       + 
                                           PAGE_WRITABLE;
                        
                        cb_dat->used_bytes += PAGE_SIZE;
                        /* Make sure that the underlying table is clean */
                        pgmgr_clear_pt(ctx, ld->level[entry]);
                    }
                }
                /* Store the level and go down again */
                ld->addr = ld->level[entry];
                ld->curr_level--;
                
            }
            else
            {
                ld->curr_level = ctx->max_level;
                ld->addr = ctx->pg_phys;
            }
        }

        ld->pos = next_pos;
    }
   
    if(ld->pos == next_pos)
    {
        ld->error = PGMGR_ERR_OK;
    }
    /* Keep going */
    return(1);
}

static phys_size_t pgmgr_free_levels_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    pgmgr_level_data_t *ld         = NULL;
    pgmgr_ctx_t        *ctx        = NULL;
    virt_addr_t        next_pos    = 0;
    uint16_t           entry       = 0;
    uint8_t            shift       = 0;
    virt_addr_t        vend        = 0;
    phys_addr_t        base        = 0;
    virt_addr_t       *up_level    = 0;
    uint8_t            up_shift    = 0;
    uint16_t           up_entry    = 0;
    
    
    ld          = pv;
    ctx         = ld->ctx;
    next_pos    = ld->pos;
    vend        = ld->base + ld->length - 1;

    cb_dat->avail_bytes = 0;
    cb_dat->phys_base   = 0;
    cb_dat->used_bytes  = 0;


    /* We're done here */
    if(vend <= ld->pos)
    {
        ld->error = PGMGR_ERR_OK;
        return(0);
    }

    ld->error = PGMGR_ERR_TBL_CREATE_FAIL;

    if(!ld->curr_level || !ld->addr)
    {
        kprintf("CURRENT_LEVEL_IS_ZERO\n");
        ld->curr_level = ctx->max_level;
        ld->addr = ctx->pg_phys;
    }
   
    while(ld->pos < vend)
    {
        if(ld->do_map || ld->level == NULL)
        {
            ld->level = (virt_addr_t*) _pgmgr_temp_map(ld->addr, 
                                                       ld->curr_level
                                                      );
            ld->do_map = 0;
        }

        shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);
        entry = (ld->pos >> shift) & 0x1FF;

#if 0
        kprintf("Current_level %d -> %x - PHYS %x " \
                "PHYS_SAVED %x ENTRY %d POS %x Increment %x\n",
                ld->curr_level, 
                ld->level,
                PAGE_MASK_ADDRESS(ld->level[entry]),
                ld->addr,
                entry, 
                ld->pos,
                (virt_size_t)1 << shift);
#endif

        /* If we haven't reached the target level
         * then we have to do down by obtaining the
         * address for the lower page table entry 
         */

        if(ld->curr_level > ld->min_level)
        {
            ld->addr = PAGE_MASK_ADDRESS(ld->level[entry]);

            if(!ld->addr)
            {
                ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                return(-1);
            }

            ld->curr_level--;
            ld->do_map = 1;
            continue;
        }
        

        if(ld->level[entry] & PAGE_PRESENT)
        {
            base = PAGE_MASK_ADDRESS(ld->level[entry]);
            
            if(cb_dat->used_bytes == 0)
            {
                cb_dat->phys_base = base;
                cb_dat->used_bytes = (virt_size_t)1 << shift;
                ld->level[entry] = 0;
            }
            else if(base == (cb_dat->phys_base + cb_dat->used_bytes))
            {
                cb_dat->used_bytes += (virt_size_t)1 << shift;
                ld->level[entry] = 0;
            }
            else
            {
                return(1);
            }
        }
        
       // if(ld->flags & PGMGR_RELEASE_LEVELS)
        {
            /* Before going up, let's see if the entry
             * from the upper level that corresponds to this
             * one can be freed
             */
            up_level = (virt_addr_t*) (pgmgr.remap_tbl + 
                            ((ld->curr_level + 1)  << PAGE_SIZE_SHIFT));
            up_shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level + 1);
            up_entry = (ld->pos >> up_shift) & 0x1FF;
          
            if(pgmgr_level_is_empty(up_level))
            {
                kprintf("LEVEL IS EMPTY\n");
            }
        }
        
        next_pos  += ((virt_size_t) 1 << shift);
        
        if(next_pos < ld->pos)
        {
            ld->error = PGMGR_ERR_OK;
            return(0);
        }

        if(((next_pos >> shift) & 0x1FF) < entry)
        {
            /* Calcuate how much we need to go up */
            while(ld->curr_level < ctx->max_level)
            {
                ld->curr_level ++;
                shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);

                if(((next_pos >> shift) & 0x1FF) >
                    ((ld->pos >> shift) & 0x1FF))
                {
                    break;
                }
            }

            /* We will map again */
            ld->do_map = 1;

            if(ld->curr_level  < ctx->max_level)
            {
               /* the upper levels should be the same so just calculate
                * the address from the base remapping table 
                */
                ld->level = (virt_addr_t*) (pgmgr.remap_tbl + 
                            (ld->curr_level  << PAGE_SIZE_SHIFT));
                
                entry = (next_pos >> shift) & 0x1FF;
                
                /* 
                 * If we are ensuring levels, check if we have an 
                 * address so that we can dive in
                 * if we don't have an address, then save it.
                 */

                
                /* Store the level and go down again */
                ld->addr = ld->level[entry];
                ld->curr_level--;
                
            }
            else
            {
                ld->curr_level = ctx->max_level;
                ld->addr = ctx->pg_phys;
            }
        }
        
        ld->pos = next_pos;
    }
   
    if(ld->pos == next_pos)
    {
        ld->error = PGMGR_ERR_OK;
    }
    /* Keep going */
    return(1);
}



static int pgmgr_setup_remap_table(pgmgr_ctx_t *ctx)
{
    pgmgr_level_data_t lvl_dat;
    uint8_t curr_level = 0;
    virt_addr_t *level    = NULL;
    phys_addr_t addr      = 0;
    uint16_t entry        = 0;
    uint8_t shift         = 0;
    int status            = 0;

    PGMGR_FILL_LEVEL(&lvl_dat, 
                     ctx, 
                     REMAP_TABLE_VADDR, 
                     REMAP_TABLE_SIZE, 
                     2, 
                     0, 
                     PGMGR_CREATE_LEVELS);

    /* Allocate pages */
    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_alloc_levels_cb, 
                          &lvl_dat);

    if(status || lvl_dat.error)
    {
        kprintf("Failed to allocate the required" 
                 "levels STATUS %x ERROR %x\n", status, lvl_dat.error);
        while(1);
    }

    addr = ctx->pg_phys;

    for(curr_level = ctx->max_level; curr_level > 0; curr_level--)
    {
        shift = PGMGR_LEVEL_TO_SHIFT(curr_level);
        entry = (REMAP_TABLE_VADDR >> shift) & 0x1FF;

        level = (virt_addr_t*)_pgmgr_temp_map(addr, curr_level);
        kprintf("ADDR %x - LEVEL %x - %x\n", addr, level, level[entry]);

        /* If we reach the bottom level, map the first page from the level 
         * to the to the table from it belongs 
         */

        if(curr_level < PGMGR_MIN_PAGE_TABLE_LEVEL)
        {
            level[entry] = addr;
            break;
        }
        addr = level[entry];
    }

    pgmgr.remap_tbl = REMAP_TABLE_VADDR;

    return(0);
}

int pgmgr_map
(
    pgmgr_ctx_t *ctx,
    virt_addr_t    virt,
    virt_size_t    length,
    phys_addr_t    phys, 
    uint32_t       attr
)
{
    pgmgr_level_data_t ld;
    pfmgr_cb_data_t    cb_data = { .avail_bytes = 0,
                                   .phys_base = 0,
                                   .used_bytes = 0
                                 };
    phys_addr_t attr_mask = 0;
    int status = 0;
    int int_status = 0;
    
    kprintf("virt %x Len %x phys %x\n",virt, length, phys);

    /* Create the tables */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     2, 
                     0, 
                     PGMGR_CREATE_LEVELS);

    spinlock_lock_int(&ctx->lock);

    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_alloc_levels_cb, 
                          &ld);
  
    if(status || ld.error)
    {
        kprintf("STATUS %x LD %x\n",status,ld.error);
        spinlock_unlock_int(&ctx->lock);
        return(-1);
    }
    /* Setup attribute mask */
    pgmgr_attr_translate(&attr_mask, attr);

    /* Do mapping */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     1, 
                     attr_mask, 
                     PGMGR_ASSIGN_ADDRESS);

    cb_data.avail_bytes = length;
    cb_data.phys_base   = phys;
    pgmgr_alloc_levels_cb(&cb_data, &ld);

    if(ld.error)
    {
        spinlock_unlock_int(&ctx->lock);
        kprintf("Failed to map %x\n", ld.error);
        return(-1);
    }
    __write_cr3(__read_cr3());
    if(__read_cr3() == ctx->pg_phys)
    {
        __write_cr3(ctx->pg_phys);
    }

    spinlock_unlock_int(&ctx->lock);

    return(0);
}

int pgmgr_alloc
(
    pgmgr_ctx_t *ctx,
    virt_addr_t    virt,
    virt_size_t    length,
    uint32_t       attr
)
{
    pgmgr_level_data_t ld;
    phys_addr_t attr_mask = 0;
    
    int int_status = 0;
    int status = 0;
    kprintf("virt %x Len %x\n",virt, length);
    /* Create the tables */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     2, 
                     0, 
                     PGMGR_CREATE_LEVELS);

    spinlock_lock_int(&ctx->lock);

    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_alloc_levels_cb, 
                          &ld);
 
    if(status || ld.error)
    {
        spinlock_unlock_int(&ctx->lock);
        return(-1);
    }
       
    /* Setup attribute mask */
    pgmgr_attr_translate(&attr_mask, attr);
    kprintf("ALLOCATING_PAGES\n");
    /* Do allocation */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     1, 
                     attr_mask,
                     PGMGR_ASSIGN_ADDRESS);

    status = pfmgr->alloc(length >> PAGE_SIZE_SHIFT, 
                          ALLOC_CB_STOP, 
                          pgmgr_alloc_levels_cb, 
                          &ld);
 
    if(ld.error || status)
    {
        kprintf("Failed to allocate %x %d\n", status, ld.error);
        spinlock_unlock_int(&ctx->lock);
        return(-1);
    }

    if(__read_cr3() == ctx->pg_phys)
    {
        __write_cr3(ctx->pg_phys);
    }

    spinlock_unlock_int(&ctx->lock);

    return(0);
}

int pgmgr_change_attrib
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len, 
    uint32_t attr
)
{

    pgmgr_level_data_t ld;
    phys_addr_t attr_mask;
    pfmgr_cb_data_t cb_data = {.avail_bytes = 0,
                               .phys_base = 0,
                               .used_bytes = 0
                               };
    int status = 0;
    
    /* Clear the level data */
    memset(&ld, 0, sizeof(pgmgr_level_data_t));

    /* Translate the attributes */
    pgmgr_attr_translate(&attr_mask, attr);

    /* Fill the level data */
    PGMGR_FILL_LEVEL(&ld, 
                      ctx, 
                      vaddr, 
                      len, 
                      1, 
                      attr_mask, 
                      PGMGR_CHANGE_ATTRIBUTES);

    cb_data.avail_bytes = len;
    /* Change the attributes */
    status = pgmgr_alloc_levels_cb(&cb_data, &ld);

    __write_cr3(__read_cr3());
    if(status || ld.error)
        return(-1);

    return(0);
}

int pgmgr_free
(
    pgmgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    return(0);
}

int pgmgr_unmap
(
    pgmgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    pgmgr_level_data_t ld;
    pfmgr_cb_data_t cb_dat = {.avail_bytes = 0, 
                              .phys_base   = 0, 
                              .used_bytes  = 0
                             };

    kprintf("BEFORE_UNMAP\n");
    kprintf("CTX %x\n",ctx);
    int i = 1;
    
    PGMGR_FILL_LEVEL(&ld, ctx, vaddr, len, 1, 0, PGMGR_RELEASE_ADDRESS);
    while(pgmgr_free_levels_cb(&cb_dat, &ld) != 0)
        kprintf("BASE %x LEN %x\n",cb_dat.phys_base, cb_dat.used_bytes);
        
        kprintf("UPPER\n");
    memset(&cb_dat, 0, sizeof(cb_dat));
    PGMGR_FILL_LEVEL(&ld, ctx, vaddr, len, 2, 0, PGMGR_RELEASE_LEVELS);

 //   while(pgmgr_free_levels_cb(&cb_dat, &ld)== 1);

kprintf("BASE %x LENGTH %x\n",cb_dat.phys_base, cb_dat.used_bytes);
  
    return(0);
}


static int pgmgr_map_kernel(pgmgr_ctx_t *ctx)
{
    /* Map code section */
    pgmgr_map(ctx, (virt_addr_t)&_code, 
                   (virt_addr_t)&_code_end - (virt_addr_t)&_code, 
                   (virt_addr_t)&_code - _KERNEL_VMA, 
                   PGMGR_EXECUTABLE);
    
    /* Map data sections */
    pgmgr_map(ctx, 
               (virt_addr_t)&_data, 
               (virt_addr_t)&_data_end -  (virt_addr_t)&_data, 
               (virt_addr_t)&_data - _KERNEL_VMA, PGMGR_WRITABLE);

    pgmgr_map(ctx, 
              (virt_addr_t)&_rodata, 
              (virt_addr_t)&_rodata_end - (virt_addr_t)&_rodata, 
              (virt_addr_t)&_rodata - _KERNEL_VMA, 0);


    pgmgr_map(ctx, 
              (virt_addr_t)&_bss, 
              (virt_addr_t)&_bss_end - (virt_addr_t)&_bss, 
              (virt_addr_t)&_bss - _KERNEL_VMA, PGMGR_WRITABLE);

    return(0);
}

/* This should be called only once */

int pgmgr_init(pgmgr_ctx_t *ctx)
{
    pat_t *pat = NULL;
    pgmgr_level_data_t lvl_dat;
    virt_addr_t top_level = 0;
    memset(&pgmgr.pat, 0, sizeof(pat_t));
    

    pat = &pgmgr.pat;

    pfmgr = pfmgr_get();

    kprintf("Initializing Page Manager\n");

    spinlock_init(&ctx->lock);
    
    pgmgr.pml5_support = pgemgr_pml5_is_enabled();
    pgmgr.nx_support   = pgmgr_check_nx();

    kprintf("PML5 %d\n", pgmgr.pml5_support);

    if(pgmgr.pml5_support)
        ctx->max_level = 5;
    else
        ctx->max_level = 4;

    
    if(pgmgr_alloc_pf(&ctx->pg_phys))
    {
        kprintf("Failed to allocate page table root\n");
        while(1);
    } 

    pgmgr_clear_pt(ctx, ctx->pg_phys);
    pgmgr_map_kernel(ctx);
    pgmgr_setup_remap_table(ctx);



    /* If we support NX, enable it */
    if(pgmgr.nx_support)
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
    kprintf("CTX %x\n",ctx->pg_phys);
    __write_cr3(ctx->pg_phys);
    kprintf("CR3 %x\n",ctx->pg_phys);
    /* Flush again */
    __wbinvd();
    

    return(0);
}

static inline virt_addr_t _pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
)
{
    virt_addr_t *remap_tbl = (virt_addr_t*)pgmgr.remap_tbl;
    virt_addr_t remap_value = 0;

    /* ix is not allowd to be 0 - root of the remapping table 
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

    remap_tbl[ix] = phys | PAGE_PRESENT | PAGE_WRITABLE;

    remap_value = pgmgr.remap_tbl + (PAGE_SIZE * ix);

    __invlpg(remap_value);
    

    return(remap_value);
}


static int _pgmgr_temp_unmap
(
    virt_addr_t vaddr
)
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


virt_addr_t pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
)
{
    if(ix < 510)
    {
        return(0);
    }

    return(_pgmgr_temp_map(phys, ix));
}

int pgmgr_temp_unmap
(
    virt_addr_t vaddr
)
{
    if(vaddr < pgmgr.remap_tbl + PAGE_SIZE * 510)
    {
        return(-1);
    }

    return(_pgmgr_temp_unmap(vaddr));
}

static inline void pgmgr_invalidate(virt_addr_t addr)
{
    /* For this CPU */
    __invlpg(addr);

    /* For other CPUs we should at least send IPI */
    cpu_issue_ipi(IPI_DEST_ALL_NO_SELF, 0, IPI_INVLPG);

}

static inline void pgmgr_invalidate_all(void)
{
    __write_cr3(__read_cr3());
    cpu_issue_ipi(IPI_DEST_ALL_NO_SELF, 0, IPI_INVLPG);
}



static int pgmgr_page_fault_handler(void *pv, isr_info_t *inf)
{

    isr_frame_t *int_frame = 0;
    virt_addr_t fault_address = 0;
    virt_addr_t error_code = *(virt_addr_t*)(inf->iframe - sizeof(uint64_t));

    fault_address = __read_cr2();
    int_frame = (isr_frame_t*)inf->iframe;

    kprintf("CPU %d: ADDRESS 0x%x ERROR 0x%x IP 0x%x SS 0x%x RFLAGS 0x%x\n",
            inf->cpu_id,   
            fault_address, 
            error_code,    
            int_frame->rip,
            int_frame->ss,
            int_frame->rflags);



    while(1);

    return(0);
}

static int pgmgr_per_cpu_invl_handler
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

int pgmgr_per_cpu_init(void)
{
    virt_addr_t cr0 = 0;
    virt_addr_t cr3 = 0;

    /* enable write protect*/
    cr0 = __read_cr0();

    cr0 |= (1 << 16);
    cr0 &= ~((1 << 29) | (1 << 30));

    __write_cr0(cr0);

    __wbinvd();
    __wrmsr(PAT_MSR, pgmgr.pat.pat);
    
    cr3 = __read_cr3();

    __write_cr3(cr3);
    
    __wbinvd();

    return(0);
}