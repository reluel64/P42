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
                        (level_data)->length = (vlength);                \
                        (level_data)->min_level = (vmin_level);             \
                        (level_data)->level = NULL;                         \
                        (level_data)->pos = vbase;                          \
                        (level_data)->current_level = (context)->max_level; \
                        (level_data)->error = 0;                            \
                        (level_data)->addr = (context)->pg_phys
                        

/* TODO: SUPPORT GUARD PAGES */

typedef struct pagemgr_path_t
{
    uint16_t      pml5_ix;
    uint16_t      pml4_ix;
    uint16_t      pdpt_ix;
    uint16_t      pdt_ix ;
    uint16_t      pt_ix  ;
    virt_addr_t   virt;
    virt_size_t   req_len;
    virt_size_t   virt_off;
    pml5e_bits_t *pml5;
    pml4e_bits_t *pml4;
    pdpte_bits_t *pdpt;
    pde_bits_t   *pd;
    pte_bits_t   *pt;
    uint32_t      attr;
    uint8_t check;
}pagemgr_path_t;

typedef struct pgmgr_level_data
{
    pagemgr_ctx_t *ctx;
    virt_addr_t base;
    virt_size_t length;
    uint8_t min_level;
    virt_addr_t *level;
    virt_size_t pos;
    uint8_t current_level;
    uint8_t error;
    phys_addr_t addr;
}pgmgr_level_data_t;



/* defines */

#define PAGE_KERNEL_MAP_STEP (0x0)
#define PAGE_REMAP_STEP      (0x1)

#define PAGE_TEMP_REMAP_PML5 (0x1)
#define PAGE_TEMP_REMAP_PML4 (0x2)
#define PAGE_TEMP_REMAP_PDPT (0x3)
#define PAGE_TEMP_REMAP_PDT  (0x4)
#define PAGE_TEMP_REMAP_PT   (0x5)

#define PAGE_TABLE_BOOT_OFFSET (0x4000)

#define PAGE_MASK_ADDRESS(x) (((x) & (~(ATTRIBUTE_MASK))))
#define PAGE_STRUCT_TEMP_MAP(x,y) pagemgr_temp_map(PAGE_MASK_ADDRESS((x)), (y))
#define PAGE_STRUCT_TEMP_UNMAP(x) pagemgr_temp_unmap(PAGE_MASK_ADDRESS((x)))

#define PAGE_PATH_RESET(path)     ((path))->pml5_ix   = ~0; \
                                  ((path))->pml4_ix   = ~0; \
                                  ((path))->pdpt_ix   = ~0; \
                                  ((path))->pdt_ix    = ~0; \
                                  ((path))->virt_off  =  (virt_size_t)0;

#define PAGEMGR_LEVEL_STACK       (5)
#define ENT_PER_TABLE             (512)
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


static int pagemgr_attr_translate(pte_bits_t *pte, uint32_t attr)
{
    if(!pte->fields.present)
    {
        return(-1);
    }
    pte->fields.read_write      = !!(attr & PGMGR_WRITABLE);
    pte->fields.user_supervisor = !!(attr & PGMGR_USER);
    pte->fields.xd              =  !(attr & PGMGR_EXECUTABLE) && 
                                    page_manager.nx_support;

    
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
        pte->fields.write_through = 0;
        pte->fields.cache_disable = 0;
        pte->fields.pat           = 0;
    }
    /* PAT 1 */
    else if(attr & PGMGR_WRITE_THROUGH)
    {
        pte->fields.write_through = 1;
        pte->fields.cache_disable = 0;
        pte->fields.pat           = 0;
    }
    /* PAT 2 */
    else if(attr & PGMGR_UNCACHEABLE)
    {
        pte->fields.write_through = 0;
        pte->fields.cache_disable = 1;
        pte->fields.pat           = 0;
    }
    /* PAT 3 */
    else if(attr & PGMGR_STRONG_UNCACHED)
    {
        pte->fields.write_through = 1;
        pte->fields.cache_disable = 1;
        pte->fields.pat           = 0;
    }
    /* PAT4 */
    else if(attr & PGMGR_WRITE_COMBINE)
    {
        pte->fields.write_through = 0;
        pte->fields.cache_disable = 0;
        pte->fields.pat           = 1;
    }
    else if(attr & PGMGR_WRITE_PROTECT)
    {
        pte->fields.write_through = 1;
        pte->fields.cache_disable = 0;
        pte->fields.pat           = 1;
    }
    /* By default do write-back */
    else
    {
        pte->fields.write_through = 0;
        pte->fields.cache_disable = 0;
        pte->fields.pat           = 0;
    }


    return(0);
}

static phys_size_t pagemgr_ensure_levels
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
    /* Make sure that the min_level is not 0 */
    if((ld->min_level == 0) || 
       (ld->min_level >= ctx->max_level))
    {
        ld->min_level = 1;
    }

    vend = ld->base + (ld->length - 1);
    next_pos = ld->pos;


    while((ld->pos < vend) && 
          (used_bytes    < total_bytes))
    {
        ld->level = (virt_addr_t*)
                          pagemgr_temp_map(ld->addr, 
                                           ld->current_level);

        shift = PGMGR_LEVEL_TO_SHIFT(ld->current_level);
        entry = (ld->pos >> shift) & 0x1FF;

#if 1
        kprintf("Current_level %d -> %x - PHYS %x PHYS_SAVED %x ENTRY %d POS %x Increment %x\n",
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

                ld->addr = ld->level[entry];
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
   
    return(used_bytes >> PAGE_SIZE_SHIFT);

}

static int pgmgr_setup_remap_table(pagemgr_ctx_t *ctx)
{
    pgmgr_level_data_t lvl_dat;
    uint8_t current_level = 0;
    virt_addr_t *level = NULL;
    phys_addr_t addr = 0;
    uint16_t entry = 0;
    uint8_t shift = 0;

    PGMGR_FILL_LEVEL(&lvl_dat, ctx, REMAP_TABLE_VADDR, REMAP_TABLE_SIZE, 2);

    pfmgr->alloc(0, ALLOC_CB_STOP, pagemgr_ensure_levels, &lvl_dat);

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

}


static int pgmgr_init_pt(pagemgr_ctx_t *ctx)
{
    
}




static int pagemgr_build_init_pagetable(pagemgr_ctx_t *ctx)
{
    pml5e_bits_t pml5e;
    pml4e_bits_t pml4e;
    pdpte_bits_t pdpte;
    pde_bits_t   pde;
    pte_bits_t   pte;
    phys_addr_t  paddr        = 0;
    phys_addr_t  phys_addr    = 0;
    virt_addr_t  vaddr        = 0;
    virt_addr_t  vbase        = 0;
    virt_size_t  req_len      = 0;
    virt_size_t  crt_len      = 0;
    virt_addr_t  *work_ptr    = 0;
    uint16_t     pml5e_ix     = -1;
    uint16_t     pml4e_ix     = -1;
    uint16_t     pdpte_ix     = -1;
    uint16_t     pde_ix       = -1;
    uint16_t     pte_ix       = -1;
    uint8_t      krnl_tbl_ok  = 0;
    uint8_t      pml5_support = 0;
    #if 0
    paddr = (uint64_t)&BOOTSTRAP_END;
    pml5_support = page_manager.pml5_support;

    /* setup the top most page table */
    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(ctx->page_phys_base);
    memset(work_ptr, 0, PAGE_SIZE);


    for(uint8_t step = 0; step < 2; step++)
    {
        if(step == PAGE_KERNEL_MAP_STEP)
        {
            vbase   = _KERNEL_VMA     + paddr;
            req_len = _KERNEL_VMA_END - vbase;
        }
        else
        {
            vbase   = REMAP_TABLE_VADDR;
            req_len = REMAP_TABLE_SIZE;
        }
        
        crt_len = 0;

        while(crt_len < req_len)
        {
            vaddr = vbase + crt_len;

            if(pml5_support)
            {
                if(pml5e_ix != VIRT_TO_PML5_INDEX(vaddr))
                {
                    pml5e_ix = VIRT_TO_PML5_INDEX(vaddr);
                    pml5e.bits = pagemgr_boot_alloc_pf();

                    if(pml5e.bits == 0)
                        return(-1);

                    pml5e.fields.present    = 1;
                    pml5e.fields.read_write = 1;

                    /* Prepare PML4 TABLE */
                    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pml5e.bits));
                    memset(work_ptr, 0, PAGE_SIZE);

                    /* Update ENTRY in PML5 */
                    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(ctx->page_phys_base));
                    memcpy(&work_ptr[pml5e_ix], &pml5e, sizeof(phys_addr_t));
                
                }
            }

            if(pml4e_ix != VIRT_TO_PML4_INDEX(vaddr))
            {
                pml4e_ix = VIRT_TO_PML4_INDEX(vaddr);
                pml4e.bits = pagemgr_boot_alloc_pf();

                if(pml4e.bits == 0)
                    return(-1);

                pml4e.fields.present = 1;
                pml4e.fields.read_write = 1;

                /* Prepare PDPT */

                work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pml4e.bits));
                memset(work_ptr, 0, PAGE_SIZE);
                        
                /* Update ENTRY in PML4 */
                if(pml5_support)
                    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pml5e.bits));
                else
                    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(ctx->page_phys_base);

                memcpy(&work_ptr[pml4e_ix], &pml4e, sizeof(phys_addr_t));
            }

            if(pdpte_ix != VIRT_TO_PDPT_INDEX(vaddr))
            {
                pdpte_ix = VIRT_TO_PDPT_INDEX(vaddr);
                pdpte.bits = pagemgr_boot_alloc_pf();

                if(pdpte.bits == 0)
                    return(-1);

                pdpte.fields.present = 1;
                pdpte.fields.read_write = 1;

                /* Prepare PDT */

                work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pdpte.bits));
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* Update ENTRY in PDPT */

                work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pml4e.bits));
                memcpy(&work_ptr[pdpte_ix], &pdpte, sizeof(phys_addr_t));
            }

            if(pde_ix != VIRT_TO_PDT_INDEX(vaddr))
            {
                pde_ix = VIRT_TO_PDT_INDEX(vaddr);
                pde.bits = pagemgr_boot_alloc_pf();

                if(pde.bits == 0)
                    return(-1);

                pde.fields.present = 1;
                pde.fields.read_write = 1;
            
                /* Prepare PT */

                work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pde.bits));
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* Update ENTRY in PDT */

                work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pdpte.bits));
                memcpy(&work_ptr[pde_ix], &pde, sizeof(phys_addr_t));
            }

            pte_ix = VIRT_TO_PT_INDEX(vaddr);

            /* set up page tables for mapping the kernel image */            
            if(step == PAGE_KERNEL_MAP_STEP)
            {
                pte.bits = paddr;

                pte.fields.xd = page_manager.nx_support & 0x1;
                /* code must be read-only and executable */
                if(vaddr >= (virt_addr_t)&_code && vaddr <=(virt_addr_t)&_code_end)
                {
                    pte.fields.read_write = 0;
                    pte.fields.xd         = 0;
                }
                /* data and bss must be read/write */
                else if((vaddr >= (virt_addr_t)&_data && vaddr <= (virt_addr_t)&_data_end) ||
                        (vaddr >= (virt_addr_t)&_bss && vaddr <= (virt_addr_t)&_bss_end))
                {
                    pte.fields.read_write = 1;
                }

                paddr += PAGE_SIZE;
                
            }
            /* create the remapping table (last 2MB) */
            else if(step == PAGE_REMAP_STEP)
            {

                
                /* Set the first entry of the page
                 * table to point to the page table itself 
                 */

                if(pte_ix == 0)
                {
                    pte.bits = pde.bits;
                    pte.fields.read_write = 1;
                    pte.fields.xd = page_manager.nx_support & 0x1;
                }
                /* other entries should be cleared */
                else
                {
                    pte.bits = 0;
                }
            }

            /* mark page as present */
            pte.fields.present = 1;
            crt_len += PAGE_SIZE;

            work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(pde.bits));
            memcpy(&work_ptr[pte_ix], &pte, sizeof(phys_addr_t));

            /* check if we are overlapping the remap table */
            if(step == PAGE_KERNEL_MAP_STEP && vaddr >= REMAP_TABLE_VADDR)
            {
                return(-1);
            }
        }
    }

    page_manager.remap_table_vaddr = REMAP_TABLE_VADDR;
 #endif
    return(0);
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
#if 0
static phys_size_t pagemgr_alloc_pages_cb(phys_addr_t phys, phys_size_t count, void *pv)
{
    phys_size_t     used_pf = 0;
    uint8_t         clear   = 0;
    virt_addr_t     virt    = 0;
    pagemgr_path_t *path    = (pagemgr_path_t*)pv;

    while((used_pf < count) && (path->virt_off < path->req_len))
    {
        virt = path->virt + path->virt_off;

        if(page_manager.pml5_support)
        {
            if(path->pml5_ix != VIRT_TO_PML5_INDEX(virt))
            {
                path->pml5_ix = VIRT_TO_PML5_INDEX(virt);
    
                if(path->pml5[path->pml5_ix].fields.present == 0)
                {
                    path->pml5[path->pml5_ix].bits = phys + used_pf * PAGE_SIZE;
                  
                    path->pml5[path->pml5_ix].fields.read_write = 1;
                    path->pml5[path->pml5_ix].fields.present    = 1;
                    
                    used_pf++;
                    clear = 1;
                }

                path->pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml5[path->pml5_ix].bits,
                                                   PAGE_TEMP_REMAP_PML4);
                if(clear)
                {
                    memset(path->pml4, 0, PAGE_SIZE);
                    clear = 0;
                    continue;
                }
            }
        }

        if(path->pml4)
        {
            
            if(path->pml4_ix != VIRT_TO_PML4_INDEX(virt))
            {
                path->pml4_ix = VIRT_TO_PML4_INDEX(virt);
             
                if(path->pml4[path->pml4_ix].fields.present == 0)
                {
                    path->pml4[path->pml4_ix].bits = phys + used_pf * PAGE_SIZE;
                    
                    path->pml4[path->pml4_ix].fields.read_write = 1;
                    path->pml4[path->pml4_ix].fields.present    = 1;
                    
                    used_pf++;
                    clear = 1;
                }
                
                path->pdpt = (pdpte_bits_t*) PAGE_STRUCT_TEMP_MAP(path->pml4[path->pml4_ix].bits,
                                               PAGE_TEMP_REMAP_PDPT);
                if(clear)
                {
                    memset(path->pdpt, 0, PAGE_SIZE);
                    clear = 0;
                    continue;
                }
            }
        }

        if(path->pdpt)
        {
            if(path->pdpt_ix != VIRT_TO_PDPT_INDEX(virt))
            {
                path->pdpt_ix = VIRT_TO_PDPT_INDEX(virt);
             
                if(path->pdpt[path->pdpt_ix].fields.present == 0)
                {
                    path->pdpt[path->pdpt_ix].bits = phys + used_pf * PAGE_SIZE;
                    
                    path->pdpt[path->pdpt_ix].fields.read_write = 1;
                    path->pdpt[path->pdpt_ix].fields.present    = 1;
                    
                    used_pf++;
                    clear = 1;
                }
                
                path->pd = (pde_bits_t*) PAGE_STRUCT_TEMP_MAP(path->pdpt[path->pdpt_ix].bits,
                                               PAGE_TEMP_REMAP_PDT);
                if(clear)
                {
                    memset(path->pd, 0, PAGE_SIZE);
                    clear = 0;
                    continue;
                }
            }
        }

        if(path->pd)
        {
            if(path->pdt_ix != VIRT_TO_PDT_INDEX(virt))
            {
                path->pdt_ix = VIRT_TO_PDT_INDEX(virt);
             
                if(path->pd[path->pdt_ix].fields.present == 0)
                {
                    path->pd[path->pdt_ix].bits = phys + used_pf * PAGE_SIZE;
                    
                    path->pd[path->pdt_ix].fields.read_write = 1;
                    path->pd[path->pdt_ix].fields.present    = 1;
                    used_pf++;
                    clear = 1;
                    path->pt = (pte_bits_t*) PAGE_STRUCT_TEMP_MAP(path->pd[path->pdt_ix].bits,
                                               PAGE_TEMP_REMAP_PT);
                }

                if(clear)
                {
                    memset(path->pt, 0, PAGE_SIZE);
                    clear = 0;
                    //pagemgr_invalidate(virt);
                }
            }
        }

        path->virt_off += PAGE_SIZE;
    }

    return(used_pf);
}

/* 
 * pagemgr_check_page_path
 * --------------------------
 * Checks if page structures need to be allocated
 */
static int pagemgr_check_page_path(pagemgr_path_t *path)
{
    virt_addr_t virt    = 0;
    
    while(path->virt_off < path->req_len)
    {
        virt = path->virt + path->virt_off;

        if(page_manager.pml5_support)
        {
            if(path->pml5_ix != VIRT_TO_PML5_INDEX(virt))
            {
                path->pml5_ix = VIRT_TO_PML5_INDEX(virt);

                if(path->pml5[path->pml5_ix].fields.present == 0)
                    return(1);

                path->pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml5[path->pml5_ix].bits,
                                                PAGE_TEMP_REMAP_PML4);
            }
        }

        if(path->pml4_ix != VIRT_TO_PML4_INDEX(virt))
        {
            path->pml4_ix = VIRT_TO_PML4_INDEX(virt);
        
            if(path->pml4[path->pml4_ix].fields.present == 0)
                return(1);

            path->pdpt = (pdpte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml4[path->pml4_ix].bits,
                                            PAGE_TEMP_REMAP_PDPT);
        }

        if(path->pdpt_ix != VIRT_TO_PDPT_INDEX(virt))
        {
            path->pdpt_ix = VIRT_TO_PDPT_INDEX(virt);

            if(path->pdpt[path->pdpt_ix].fields.present == 0)
                return(1);

            path->pd = (pde_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pdpt[path->pdpt_ix].bits,
                                            PAGE_TEMP_REMAP_PDT);
                                         
        }

        if(path->pdt_ix != VIRT_TO_PDT_INDEX(virt))
        {
            path->pdt_ix = VIRT_TO_PDT_INDEX(virt);

            if(path->pd[path->pdt_ix].fields.present == 0)
                return(1);
        }    

        path->virt_off += PAGE_SIZE;
    }
    return(0);
}

static phys_size_t pagemgr_alloc_or_map_cb
(
    phys_addr_t phys, 
    phys_size_t count, 
    void *pv
)
{
    pagemgr_path_t *path = (pagemgr_path_t*)pv;
    phys_size_t used_pf     = 0;
    virt_addr_t virt        = 0;
    
    while(used_pf < count && path->virt_off < path->req_len)
    {
        virt = path->virt + path->virt_off;

        if(path->attr & PAGE_GUARD)
        {
            if(path->virt_off == 0)
            {
                //pagemgr_invalidate(virt);
                path->virt_off += PAGE_SIZE;
                continue;
            }
            else if(path->virt_off + PAGE_SIZE >= path->req_len)
            {
                path->virt_off += PAGE_SIZE;
                //pagemgr_invalidate(virt);
                break;
            }
        }

        if(page_manager.pml5_support)
        {
            if(path->pml5_ix != VIRT_TO_PML5_INDEX(virt))
            {
                path->pml5_ix = VIRT_TO_PML5_INDEX(virt);

                path->pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml5[path->pml5_ix].bits,
                                                PAGE_TEMP_REMAP_PML4);
            }
        }

        if(path->pml4_ix != VIRT_TO_PML4_INDEX(virt))
        {
            path->pml4_ix = VIRT_TO_PML4_INDEX(virt);
        
            path->pdpt = (pdpte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml4[path->pml4_ix].bits,
                                            PAGE_TEMP_REMAP_PDPT);
        }

        if(path->pdpt_ix != VIRT_TO_PDPT_INDEX(virt))
        {
            path->pdpt_ix = VIRT_TO_PDPT_INDEX(virt);
            path->pd = (pde_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pdpt[path->pdpt_ix].bits,
                                            PAGE_TEMP_REMAP_PDT);

        }

        if(path->pdt_ix != VIRT_TO_PDT_INDEX(virt))
        {
            path->pdt_ix = VIRT_TO_PDT_INDEX(virt);
            path->pt = (pte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pd[path->pdt_ix].bits,
                                            PAGE_TEMP_REMAP_PT);
        }
       
        path->pt_ix = VIRT_TO_PT_INDEX(virt);
        
        if(path->pt[path->pt_ix].fields.present == 0)
        {
            path->pt[path->pt_ix].bits = phys + used_pf * PAGE_SIZE;
            path->pt[path->pt_ix].fields.present = 1;
            path->pt[path->pt_ix].fields.dirty = 0;
            path->pt[path->pt_ix].fields.accessed = 0;
        }
        
        /* Any attempt to apply attributes to a non-existent page
         * will cause the function to exit 
         */

        if(pagemgr_attr_translate(&path->pt[path->pt_ix], path->attr))
            return(0);
            
        //pagemgr_invalidate(virt);

        path->virt_off += PAGE_SIZE;
        used_pf++;
    }
    
    return(used_pf);
}

static int pagemgr_free_or_unmap_cb
(
    phys_addr_t *phys, 
    phys_size_t *count, 
    void *pv
)
{
    pagemgr_path_t *path       = (pagemgr_path_t*)pv;
    phys_addr_t     start_phys = 0;
    phys_size_t     page_count = 0;
    phys_addr_t     page_phys  = 0;
    virt_addr_t     virt       = 0;

    while(path->virt_off < path->req_len)
    {
        virt = path->virt + path->virt_off;

        if(page_manager.pml5_support)
        {
            if(path->pml5_ix != VIRT_TO_PML5_INDEX(virt))
            {
                path->pml5_ix = VIRT_TO_PML5_INDEX(virt);

                path->pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml5[path->pml5_ix].bits,
                                                PAGE_TEMP_REMAP_PML4);
            }
        }

        if(path->pml4_ix != VIRT_TO_PML4_INDEX(virt))
        {
            path->pml4_ix = VIRT_TO_PML4_INDEX(virt);
        
            path->pdpt = (pdpte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml4[path->pml4_ix].bits,
                                            PAGE_TEMP_REMAP_PDPT);
        }

        if(path->pdpt_ix != VIRT_TO_PDPT_INDEX(virt))
        {
            path->pdpt_ix = VIRT_TO_PDPT_INDEX(virt);

            path->pd = (pde_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pdpt[path->pdpt_ix].bits,
                                            PAGE_TEMP_REMAP_PDT);
        }

        if(path->pdt_ix != VIRT_TO_PDT_INDEX(virt))
        {
            path->pdt_ix = VIRT_TO_PDT_INDEX(virt);
            path->pt = (pte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pd[path->pdt_ix].bits,
                                            PAGE_TEMP_REMAP_PT);
        }
       
        path->pt_ix    = VIRT_TO_PT_INDEX(virt);
        page_phys = PAGE_MASK_ADDRESS(path->pt[path->pt_ix].bits);
      
        if(path->pt[path->pt_ix].fields.present)
        {
            /* No pages in counter - set up starting address */
            if(page_count == 0)
            {
                start_phys = page_phys;
                page_count = 1;
                path->pt[path->pt_ix].bits = 0;

                /* Make sure that this entry is flushed from TLB */
                //pagemgr_invalidate(virt);
                path->virt_off += PAGE_SIZE;
                continue;
            }
            /* See if this page is in chain with start_phys */
            else if(page_phys == (start_phys + (page_count * PAGE_SIZE)))
            {
                page_count++;
                path->pt[path->pt_ix].bits = 0;
                /* Make sure that this entry is flushed from TLB */
                //pagemgr_invalidate(virt);

                path->virt_off += PAGE_SIZE;
                continue;
            }
            /* Current page is not in the chain - let's send
             * what we got so far to the PFMGR
             */ 
            else
            {
                //pagemgr_invalidate(virt);
                *phys = start_phys;
                *count = page_count;
                return(1);
            }
        }
        else if(page_phys != 0)
        {
            kprintf("STOP: VIRT 0x%x - BITS 0x%x\n", virt,path->pt[path->pt_ix].bits );
            while(1);
        }
    }

    *phys = start_phys;
    *count = page_count;

    return(0);
}

/* 
 * pagemgr_build_page_path
 * --------------------------
 * Allocates pages used for address
 * translation
 */

static int pagemgr_build_page_path(pagemgr_path_t *path)
{
    int        ret  =  0;
    virt_size_t virt_off = 0;

    /* Check if we have page path already built */
    
    PAGE_PATH_RESET(path);
    
    ret = pagemgr_check_page_path(path);
    
    /* save the offset before reseting the path */
    virt_off = path->virt_off;

    /* Reset the path */
    PAGE_PATH_RESET(path);

    /* Restore the offset */
    path->virt_off = virt_off;

    if(ret == 0)
        return(0);
    
    ret = pfmgr->alloc(PAGE_SIZE, ALLOC_CB_STOP, 
                       pagemgr_alloc_pages_cb, (void*)path);
    
    if(ret == -1 || path->req_len > path->virt_off)
        return(-1);

    /* Reset the path */
    PAGE_PATH_RESET(path);

    return(0);
}
#endif
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
    #if 0
    pagemgr_path_t path;
    phys_addr_t    pg_frames  = 0;
    phys_addr_t    ret        = 0;
    int            int_status = 0;

    memset(&path, 0, sizeof(pagemgr_path_t));
    
    path.virt     = vaddr;
    path.req_len  = len;
    path.attr     = attr;
    path.check = 1;
    pg_frames = len / PAGE_SIZE;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);

    PAGE_PATH_RESET(&path);
    
    spinlock_lock_int(&ctx->lock, &int_status);
    
    ret = pagemgr_alloc_or_map_cb(0, pg_frames, &path) == pg_frames;
    ret = ret ? 0 : -1;
    pagemgr_invalidate_all();
    spinlock_unlock_int(&ctx->lock, int_status);

    return((int)ret);
    #endif
    return(0);
}

int pagemgr_free
(
    pagemgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    #if 0
    int ret = 0;
    pagemgr_path_t path;
    int int_status = 0;
    memset(&path,0,sizeof(path));
    
    path.virt = vaddr;
    path.req_len = len;
    
    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    
    PAGE_PATH_RESET(&path);
    
    spinlock_lock_int(&ctx->lock, &int_status);
  
    ret = pagemgr_check_page_path(&path);

    if(ret != 0)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    PAGE_PATH_RESET(&path);
    
    pfmgr->dealloc(pagemgr_free_or_unmap_cb, &path);
    pagemgr_invalidate_all();
    spinlock_unlock_int(&ctx->lock, int_status);

    return(0);
    #endif

    return(0);
}

int pagemgr_unmap
(
    pagemgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    #if 0
    int ret = 0;
    pagemgr_path_t path;
    phys_addr_t dummy_phys  = 0;
    phys_size_t dummy_count = 0;
    int int_status = 0;

    memset(&path,0,sizeof(path));

    path.virt    = vaddr;
    path.req_len = len;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(ctx->page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);

    spinlock_lock_int(&ctx->lock, &int_status);

    PAGE_PATH_RESET(&path);
    
    ret = pagemgr_check_page_path(&path);
    
    if(ret != 0)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    PAGE_PATH_RESET(&path);

    while(pagemgr_free_or_unmap_cb(&dummy_phys, &dummy_count, &path));

    pagemgr_invalidate_all();

    spinlock_unlock_int(&ctx->lock, int_status);
    
    return(0);
    #endif

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