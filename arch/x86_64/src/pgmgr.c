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


#define PGMGR_LEVEL_TO_SHIFT(x) (PT_SHIFT + (((x) - 1) << 3) + ((x) - 1))
#define PGMGR_ENTRIES_PER_LEVEL (512)
#define PGMGR_FILL_LEVEL(ld, context, _base,                           \
                        _length, _req_level, _attr, _cb)               \
                        (ld)->ctx        = (context);                  \
                        (ld)->base       = (_base); \
                        (ld)->level      = NULL;                       \
                        (ld)->length     = (_length);                  \
                        (ld)->offset     = (0);                        \
                        (ld)->level_phys = (context)->pg_phys;         \
                        (ld)->req_level  = (_req_level);               \
                        (ld)->curr_level = (context)->max_level;       \
                        (ld)->error      = 1;                          \
                        (ld)->do_map     = 1;                          \
                        (ld)->attr_mask  = (_attr);                    \
                        (ld)->iter_cb    = (_cb);                      \
                        (ld)->cb_status  = 0
                        
#define PAGE_MASK_ADDRESS(x)                 (((x) & (~(ATTRIBUTE_MASK))))
#define PGMGR_MIN_PAGE_TABLE_LEVEL (0x2)
#define PGMGR_LEVEL_TO_STEP(lvl)              (((virt_size_t)1 << \
                                            PGMGR_LEVEL_TO_SHIFT((lvl))))
#define PGMGR_CLEAR_PT_PAGE(max_level)      ((max_level) + 1)
#define PGMGR_LEVEL_ENTRY_PAGE(max_level)   ((max_level) + 2)

/* Error codes */
#define PGMGR_ERR_OK                  (0)
#define PGMGR_ERR_NO_FRAMES           (1 << 0)
#define PGMGR_ERR_TABLE_NOT_ALLOCATED (1 << 1)
#define PGMGR_ERR_TBL_CREATE_FAIL     (1 << 2)

/* Flags for the iterator callback */
#define PGMGR_CB_LEVEL_GO_DOWN      (1 << 0)
#define PGMGR_CB_LEVEL_GO_UP        (1 << 1)
#define PGMGR_CB_NEXT_ENTRY         (1 << 2)
#define PGMGR_CB_DO_REQUEST         (1 << 3)
#define PGMGR_CB_RES_CHECK          (1 << 4)

/* Return codes for the iter callbacks */

#define PGMGR_CB_AGAIN              (1 << 0)
#define PGMGR_CB_ERROR              (1 << 1)
#define PGMGR_CB_BREAK              (1 << 2)
#define PGMGR_CB_GO_UP              (1 << 3)

/* forward declarations */
typedef struct pgmgr_iter_callback_data_t pgmgr_iter_callback_data_t;
typedef struct pgmgr_level_data_t         pgmgr_level_data_t;

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
    virt_size_t offset;
    phys_addr_t level_phys;
    uint8_t     req_level;
    uint8_t     curr_level;
    uint8_t     error;
    uint8_t     do_map;
    phys_size_t attr_mask;
    uint32_t    cb_status;
    void        (*iter_cb)
    (
        pgmgr_iter_callback_data_t *ic, 
        pgmgr_level_data_t *ld,
        pfmgr_cb_data_t *pfmgr_dat,
        uint32_t op
    );
}pgmgr_level_data_t;

typedef struct pgmgr_iter_callback_data_t
{
    /* Status for the iter callback */
    virt_addr_t         vaddr;
    uint16_t            entry;
    uint8_t             shift;
    virt_addr_t         next_vaddr;
    virt_size_t         increment;
}pgmgr_iter_callback_data_t;



/* locals */
static pgmgr_t pgmgr;
static pfmgr_t *pfmgr        = NULL;

static int         pgmgr_page_fault_handler
(
    void *pv, 
    isr_info_t *inf
);

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

static int pgmgr_alloc_pf_cb
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

static void pgmgr_clear_pt
(
    pgmgr_ctx_t *ctx,
    phys_addr_t addr
)
{
    virt_addr_t vaddr = 0;

    vaddr = _pgmgr_temp_map(addr, 
                            PGMGR_CLEAR_PT_PAGE(ctx->max_level));

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

static int pgmgr_level_entry_is_empty
(
    pgmgr_ctx_t *ctx,
    phys_addr_t addr
)
{
    virt_addr_t *vaddr = NULL;
    int ret = -1;

    vaddr = (virt_addr_t*)_pgmgr_temp_map(addr, 
                                         PGMGR_LEVEL_ENTRY_PAGE(ctx->max_level));
    if(vaddr != NULL)
    {
        ret = pgmgr_level_is_empty(vaddr);
        _pgmgr_temp_unmap((virt_addr_t)vaddr);
    }
    
    return(ret);
}

static void pgmgr_dump_level(virt_addr_t *lvl)
{
    for(int i = 0; i < PGMGR_ENTRIES_PER_LEVEL; i++)
    {
        if(lvl[i])
        kprintf("ENTRY %d -> %x\n",i, lvl[i]);
    }
}

static void pgmgr_iter_free_level
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    phys_addr_t addr = 0;
    virt_addr_t *up_level = NULL;
    uint8_t shift = 0;
    uint16_t entry = 0;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        {
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->cb_status |= PGMGR_CB_ERROR;
                ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
            }

            ld->cb_status |= PGMGR_CB_AGAIN;

            break;
        }

        case PGMGR_CB_NEXT_ENTRY:
        {
            if(ld->offset == ld->length)
            {
                ld->cb_status |= PGMGR_CB_GO_UP;
            }
            break;
        }

       case PGMGR_CB_DO_REQUEST:
       /* Intentionally fall through */
       case PGMGR_CB_LEVEL_GO_UP:
       {   
           addr = PAGE_MASK_ADDRESS(ld->level[iter_dat->entry]);

           if(pgmgr_level_entry_is_empty(ld->ctx, addr) > 0)
           {
                /* first try */
                if(pfmgr_dat->used_bytes == 0)
                {
                    pfmgr_dat->used_bytes = PAGE_SIZE;
                    pfmgr_dat->phys_base = addr;
                    ld->level[iter_dat->entry] = 0;
                }
                /* add from right */
                else if(addr  ==
                       (pfmgr_dat->phys_base +
                        pfmgr_dat->used_bytes) )
                {
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                /* add from left */
                else if(addr + PAGE_SIZE == pfmgr_dat->phys_base)
                {
                    pfmgr_dat->phys_base = addr;
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                /* we cannot add as the address is not contigous
                 * so we will send what we got so far and then
                 * try again
                 */
                else
                {
                    kprintf("BREAKING: VADDR %x NEXT_VADDR %x\n",iter_dat->vaddr, iter_dat->next_vaddr);
                    ld->cb_status |= PGMGR_CB_BREAK;    
                }
           }

           if(ld->offset == ld->length)
           {
               ld->cb_status |= PGMGR_CB_AGAIN;
           }

           break;
       }

    }
}

static void pgmgr_iter_alloc_level
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    pgmgr_ctx_t *ctx = NULL;
    
    virt_size_t len = 0;
    virt_addr_t new_offset = 0;
    ctx       = ld->ctx;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        {
            ld->cb_status |= PGMGR_CB_AGAIN;
        }
        /* fall through */
        case PGMGR_CB_DO_REQUEST:
        {
            
             /* check if the page table entry is present */
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->level[iter_dat->entry] = pfmgr_dat->phys_base  + 
                                             pfmgr_dat->used_bytes + 
                                             PAGE_PRESENT          + 
                                             PAGE_WRITABLE;

                /* Make sure that the underlying table is clean */
                pfmgr_dat->used_bytes += PAGE_SIZE;

                pgmgr_clear_pt(ctx, ld->level[iter_dat->entry]);
            } 
           
            break;
        }
        case PGMGR_CB_RES_CHECK:
        {
             
            if(pfmgr_dat->avail_bytes <= pfmgr_dat->used_bytes)
            {
                ld->cb_status |= PGMGR_CB_BREAK;
               
                /* If we are at the edge but we have one more entry to 
                 * allocate, set the PGMGR_CB_AGAIN flag so that we
                 * can allocate memory for the remaining entry
                 */
#if 0
                if(ld->offset == ld->length)
                {
                    ld->cb_status |= PGMGR_CB_AGAIN;
                }
#endif
            }
             
            break;
        }

        case PGMGR_CB_NEXT_ENTRY:
        {
            if(ld->offset == ld->length)
            {
                ld->cb_status |= PGMGR_CB_AGAIN;
            }
            break;
        }
    }
}

/* 
 * pgmgr_iter_free_page - frees / unmaps a page
 */

static void pgmgr_iter_free_page
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    phys_addr_t addr = 0;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        {
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->cb_status |= PGMGR_CB_ERROR;
                ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
            }
            else
            {
                ld->cb_status |= PGMGR_CB_AGAIN;
            }
            break;
        }
        
        case PGMGR_CB_DO_REQUEST:
        {
            addr = PAGE_MASK_ADDRESS(ld->level[iter_dat->entry]);
            
            if(ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                if(pfmgr_dat->used_bytes == 0)
                {
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    pfmgr_dat->phys_base = addr;
                    ld->level[iter_dat->entry] = 0;
                }
                else if((pfmgr_dat->phys_base + pfmgr_dat->used_bytes) == addr)
                {
                    pfmgr_dat->used_bytes+=PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                else if(addr + PAGE_SIZE == pfmgr_dat->phys_base)
                {
                    pfmgr_dat->phys_base = addr;
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                else
                {
                    ld->cb_status |= PGMGR_CB_BREAK;
                }
            }

            break;
        }
    }
}

/* 
 * pgmgr_iter_alloc_page - allocates / maps a page
 */

static void pgmgr_iter_alloc_page
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    pgmgr_ctx_t        *ctx = NULL;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
             if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
             {
                 kprintf("LEVEL %d VADDR %x\n",ld->curr_level, iter_dat->vaddr);
                 ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                 ld->cb_status |= PGMGR_CB_ERROR;
             }
             else
             {
                 ld->cb_status = PGMGR_CB_AGAIN;
             }
             break;
        
        case PGMGR_CB_DO_REQUEST:
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->level[iter_dat->entry] = (pfmgr_dat->phys_base + 
                                              pfmgr_dat->used_bytes) | 
                                              ld->attr_mask          | 
                                              PAGE_PRESENT;

                pfmgr_dat->used_bytes += PAGE_SIZE;
            }
            break;

        case PGMGR_CB_RES_CHECK:
            if(pfmgr_dat->avail_bytes <= pfmgr_dat->used_bytes)
                ld->cb_status |= PGMGR_CB_BREAK;
            break;
    }
}

/*
 * pgmgr_iter_change_attribs - change page attributes
 */
static void pgmgr_iter_change_attribs
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    pgmgr_ctx_t        *ctx = NULL;

    ld->error  = PGMGR_ERR_OK;
    
    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
             if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
             {
                 ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                 ld->cb_status |= PGMGR_CB_ERROR;
             }
             break;

        case PGMGR_CB_DO_REQUEST:
            if(ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->level[iter_dat->entry] = PAGE_MASK_ADDRESS(
                                              ld->level[iter_dat->entry]) | 
                                              ld->attr_mask               |
                                              PAGE_PRESENT;
             
                pfmgr_dat->used_bytes += PAGE_SIZE;
            }
            break;
    }
}

static int pgmgr_iterate_levels
(
    pfmgr_cb_data_t *pfmgr_dat,
    void            *pv
)
{
    pgmgr_level_data_t *ld        = NULL;
    pgmgr_ctx_t       *ctx        = NULL;
    pgmgr_iter_callback_data_t it_dat = {
                                            .entry = 0,
                                            .increment = 0,
                                            .next_vaddr = 0,
                                            .shift = 0,
                                            .vaddr = 0,    
                                        };

    ld          = pv;
    ctx         = ld->ctx;

    /* If the offset is bigger than the length, then we're done here
     * However, if the callback requires the iteration to go one more time,
     * we can skip the ckeck
     */
    if((ld->length  <= ld->offset) && (~ld->cb_status & PGMGR_CB_AGAIN))
    {
        return(0);
    }

    if(!ld->curr_level || !ld->level_phys)
    {
        ld->curr_level = ctx->max_level;
        ld->level_phys = ctx->pg_phys;
        ld->do_map     = 1;
    }
    
    while(ld->offset < ld->length || 
         (ld->cb_status & PGMGR_CB_AGAIN))
    {
        ld->cb_status = 0;

        /* Check if there are resources available */
        ld->iter_cb(&it_dat,
                    ld,
                    pfmgr_dat,
                    PGMGR_CB_RES_CHECK);

        if(ld->cb_status  & PGMGR_CB_ERROR)
        {
            return(-1);
        }
        else if(ld->cb_status & PGMGR_CB_BREAK)
        {
            break;
        }

        /* Map the page table */
        if(ld->do_map || ld->level == NULL)
        {
            ld->level = (virt_addr_t*) _pgmgr_temp_map(ld->level_phys, 
                                                       ld->curr_level);
            ld->do_map = 0;
        }

        it_dat.shift     = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);
        it_dat.increment = (virt_size_t)1 << it_dat.shift;
        it_dat.vaddr     = ld->base + ld->offset, 
        it_dat.entry     = (it_dat.vaddr >> it_dat.shift) & 0x1FF; 

        /* If we haven't reached the target level then we have to go
         * down by obtaining the address for the lower page table entry 
         */

        if(ld->curr_level > ld->req_level)
        {
            /* notify the callbnack the we have to go down 
             * so it notifies if we can continue or not
             */
            ld->iter_cb(&it_dat,
                         ld,
                         pfmgr_dat,
                         PGMGR_CB_LEVEL_GO_DOWN);

            if(ld->cb_status & PGMGR_CB_ERROR)
            {
                return (-1);
            }
            else if(ld->cb_status & PGMGR_CB_BREAK)
            {
                break;
            }

            ld->level_phys = ld->level[it_dat.entry];
            ld->curr_level--;
            ld->do_map = 1;

            continue;
        }

        /* do the actual request - allocate/free/change attrs */
        ld->iter_cb(&it_dat, 
                    ld,
                    pfmgr_dat,
                    PGMGR_CB_DO_REQUEST);
 
        if(ld->cb_status & PGMGR_CB_ERROR)
        {
            return(-1);
        }
        else if(ld->cb_status & PGMGR_CB_BREAK)
        {
            break;
        }
 
        ld->offset += it_dat.increment;
        it_dat.next_vaddr = ld->base + ld->offset;

        ld->iter_cb(&it_dat, 
                    ld,
                    pfmgr_dat,
                    PGMGR_CB_NEXT_ENTRY);

        if(ld->cb_status & PGMGR_CB_ERROR)
        {
            return(-1);
        }
        else if(ld->cb_status & PGMGR_CB_BREAK)
        {
            break;
        }

        /* Check if we need to switch the level */
        if((((it_dat.next_vaddr >> it_dat.shift) & 0x1FF) < it_dat.entry) ||
            (ld->cb_status & PGMGR_CB_GO_UP))
        {
            /* Calculate how much we need to go up */
            while(ld->curr_level < ctx->max_level)
            {           
                /* set up the level to point to the upper structure */     
                ld->curr_level++;

                /* calulcate the shift */
                it_dat.shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);
                
                /* calculate the entry for the upper level */
                it_dat.entry = (it_dat.vaddr >> it_dat.shift) & 0x1FF;

                /* save the physical address of the upper level */
                ld->level_phys = 
                             ((virt_addr_t*)pgmgr.remap_tbl)[ld->curr_level];

                /* save the upper level */
                ld->level = (virt_addr_t*) (pgmgr.remap_tbl + 
                                           (ld->curr_level  << PAGE_SIZE_SHIFT));
                /* Tell the callback that we are going up so it may
                 * be able to free level entries
                 */
                ld->iter_cb(&it_dat, 
                            ld,
                            pfmgr_dat,
                            PGMGR_CB_LEVEL_GO_UP);
                
                if(ld->cb_status & PGMGR_CB_ERROR)
                {
                    return(-1);
                }
                else if(ld->cb_status & PGMGR_CB_BREAK)
                {
                    /* we are needing the same vaddr again so 
                     * subtract the offset 
                     */
                    ld->offset -= it_dat.increment;
                    break;
                }

                /* If we might found the position, check if there 
                 * is a entry in the level. If it's not, then we will keep
                 * looking
                 */
                
                if(((it_dat.next_vaddr >> it_dat.shift) & 0x1FF) >
                    ((it_dat.vaddr >> it_dat.shift) & 0x1FF))
                {
                    it_dat.entry = (it_dat.next_vaddr >> it_dat.shift) & 0x1FF;
                    
                    if(ld->level[it_dat.entry] & PAGE_PRESENT)
                        break;
                }
            }
            /* Check if we've max level and if we did, 
             * begin from the top level 
             */
            if(ld->curr_level >= ctx->max_level)
            {
                ld->level = NULL;
                ld->level_phys = ctx->pg_phys;
                ld->curr_level = ctx->max_level;
                ld->do_map = 1;
            }
        }

        if(ld->cb_status & PGMGR_CB_BREAK)
            break;
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
    
    kprintf("----Setting up remapping table----\n");
    
    PGMGR_FILL_LEVEL(&lvl_dat, 
                     ctx, 
                     REMAP_TABLE_VADDR, 
                     REMAP_TABLE_SIZE, 
                     2, 
                     0, 
                     pgmgr_iter_alloc_level);

    /* Allocate pages */
    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_iterate_levels, 
                          &lvl_dat);

    if(status || lvl_dat.error)
    {
        kprintf("Failed to allocate the required " 
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
    kprintf("----Done setting up remapping table----\n");
    return(0);
}

int pgmgr_map
(
    pgmgr_ctx_t *ctx,
    virt_addr_t virt,
    virt_size_t length,
    phys_addr_t phys, 
    uint32_t    attr
)
{
    pgmgr_level_data_t ld;
    pfmgr_cb_data_t    cb_data = { .avail_bytes = 0,
                                   .phys_base = 0,
                                   .used_bytes = 0
                                 };
    phys_addr_t attr_mask = 0;
    int status = 0;
    kprintf("CREATING TABLES\n");
    /* Create the tables */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     2, 
                     0, 
                     pgmgr_iter_alloc_level);

    spinlock_lock_int(&ctx->lock);

    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_iterate_levels, 
                          &ld);
  
    if(status || ld.error)
    {
        kprintf("STATUS %x LD %x\n",status,ld.error);
        spinlock_unlock_int(&ctx->lock);
        return(-1);
    }
    /* Setup attribute mask */
    pgmgr_attr_translate(&attr_mask, attr);
    kprintf("MAPPING ADDRESS\n");
    /* Do mapping */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     1, 
                     attr_mask, 
                     pgmgr_iter_alloc_page);

    cb_data.avail_bytes = length;
    cb_data.phys_base   = phys;
    status = pgmgr_iterate_levels(&cb_data, &ld);

    if(status < 0 || ld.error)
    {
        spinlock_unlock_int(&ctx->lock);
        kprintf("Failed to map STATUS %d ERR %d \n", status, ld.error);
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
extern int pfmgr_show;
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
   // kprintf("ALIGNED %x\n",ALIGN_DOWN(virt, vir));
    /* Create the tables */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     2, 
                     0, 
                     pgmgr_iter_alloc_level);

    spinlock_lock_int(&ctx->lock);
kprintf("ALLOC LEVELS\n");
pfmgr_show = 1;
    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_iterate_levels, 
                          &ld);
    pfmgr_show = 0;
 kprintf("DONE ALLOC\n");
    if(status || ld.error)
    {
        kprintf("STATUS %x ERROR %x\n",status, ld.error);
        spinlock_unlock_int(&ctx->lock);
        return(-1);
    }
       
    /* Setup attribute mask */
    pgmgr_attr_translate(&attr_mask, attr);
   
    /* Do allocation */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     virt, 
                     length, 
                     1, 
                     attr_mask,
                     pgmgr_iter_alloc_page);

    status = pfmgr->alloc(length >> PAGE_SIZE_SHIFT, 
                          ALLOC_CB_STOP, 
                          pgmgr_iterate_levels, 
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
                      pgmgr_iter_change_attribs);

    cb_data.avail_bytes = len;
    /* Change the attributes */
    status = pgmgr_iterate_levels(&cb_data, &ld);

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
    pgmgr_level_data_t ld;
    int status = -1;
    
    spinlock_lock_int(&ctx->lock);
    
    PGMGR_FILL_LEVEL(&ld, ctx, vaddr, len, 1, 0, pgmgr_iter_free_page);
    status = pfmgr->dealloc(pgmgr_iterate_levels, &ld);
    
    if(status || ld.error)
    {
          __write_cr3(__read_cr3());
        spinlock_unlock_int(&ctx->lock);
        return(status);
    }   

    PGMGR_FILL_LEVEL(&ld, ctx, vaddr, len , 2, 0, pgmgr_iter_free_level);

    kprintf("FREEING_LEVELS\n");
    status = pfmgr->dealloc(pgmgr_iterate_levels, &ld);

    if(status || ld.error)
    {
        status = -1;
    }
    else
    {
        status = 0;
    }
    kprintf("DONE FREEING\n");

      __write_cr3(__read_cr3());
    spinlock_unlock_int(&ctx->lock);
 
    return(status);
}

int pgmgr_unmap
(
    pgmgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    pgmgr_level_data_t ld;
    pfmgr_cb_data_t pfmgr_dat = {.avail_bytes = 0, 
                                 .phys_base   = 0, 
                                 .used_bytes  = 0
                             };
    int status = -1;

    spinlock_lock_int(&ctx->lock);
    
    PGMGR_FILL_LEVEL(&ld, ctx, vaddr, len, 1, 0, pgmgr_iter_free_page);
    do
    {
        status = pgmgr_iterate_levels(&pfmgr_dat, &ld);
        pfmgr_dat.avail_bytes = 0;
        pfmgr_dat.phys_base   = 0;
        pfmgr_dat.used_bytes  = 0;
        kprintf("STATUS %x LD_ERR %x\n",status, ld.error);
    }while((status > 0) && (ld.error == PGMGR_ERR_OK));

    if(status || ld.error)
    {
        status = -1;
        spinlock_unlock_int(&ctx->lock);
        return(status);
    }

    PGMGR_FILL_LEVEL(&ld, ctx, vaddr, len, 2, 0, pgmgr_iter_free_level);
    status = pfmgr->dealloc(pgmgr_iterate_levels, &ld);

    __write_cr3(__read_cr3());
    
    if(status || ld.error)
    {
        status = -1;
    }
    else
    {
        status = 0;
    }

    spinlock_unlock_int(&ctx->lock);
    
    return(status);
}


static int pgmgr_map_kernel(pgmgr_ctx_t *ctx)
{
    kprintf("Mapping kernel sections\n");
    /* Map code section */
    pgmgr_map(ctx, (virt_addr_t)&_code, 
                   (virt_addr_t)&_code_end - (virt_addr_t)&_code, 
                   (virt_addr_t)&_code - _KERNEL_VMA, 
                   PGMGR_EXECUTABLE);
    
    /* Map data sections */
    pgmgr_map(ctx, (virt_addr_t)&_data, 
                   (virt_addr_t)&_data_end -  (virt_addr_t)&_data, 
                   (virt_addr_t)&_data - _KERNEL_VMA, 
                   PGMGR_WRITABLE);

    pgmgr_map(ctx, (virt_addr_t)&_rodata, 
                   (virt_addr_t)&_rodata_end - (virt_addr_t)&_rodata, 
                   (virt_addr_t)&_rodata - _KERNEL_VMA, 
                   0);

    pgmgr_map(ctx, (virt_addr_t)&_bss, 
                   (virt_addr_t)&_bss_end - (virt_addr_t)&_bss, 
                   (virt_addr_t)&_bss - _KERNEL_VMA, 
                   PGMGR_WRITABLE);
    kprintf("Done mapping kernel sections\n");
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
