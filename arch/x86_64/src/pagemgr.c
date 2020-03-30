/* Paging management 
 * This file contains code that would 
 * allow us to manage paging 
 * structures
 */

#include <physmm.h>
#include <stdint.h>
#include <paging.h>
#include <utils.h>
#include <pagemgr.h>
#include <isr.h>
#include <spinlock.h>
#include <vmmgr.h>

typedef struct 
{
    phys_addr_t page_phys_base; /* physical location of the first
                              * level of paging
                              */ 
    virt_addr_t remap_table_vaddr;
    uint8_t     pml5_support;
    uint8_t     do_nx;
    spinlock_t  lock;
}pagemgr_root_t;

typedef struct
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


/* defines */

#define PAGE_KERNEL_MAP_STEP (0x0)
#define PAGE_REMAP_STEP      (0x1)

#define PAGE_TEMP_REMAP_PML5 (0x1)
#define PAGE_TEMP_REMAP_PML4 (0x2)
#define PAGE_TEMP_REMAP_PDPT (0x3)
#define PAGE_TEMP_REMAP_PDT  (0x4)
#define PAGE_TEMP_REMAP_PT   (0x5)

#define PAGE_MASK_ADDRESS(x) (((x) & (~(ATTRIBUTE_MASK))))
#define PAGE_STRUCT_TEMP_MAP(x,y) pagemgr_temp_map(PAGE_MASK_ADDRESS((x)), (y))
#define PAGE_STRUCT_TEMP_UNMAP(x) pagemgr_temp_unmap(PAGE_MASK_ADDRESS((x)))

#define PAGE_PATH_RESET(path)     ((path))->pml5_ix   = ~0; \
                                  ((path))->pml4_ix   = ~0; \
                                  ((path))->pdpt_ix   = ~0; \
                                  ((path))->pdt_ix    = ~0; \
                                  ((path))->virt_off  =  (virt_size_t)0;
/* externs */

extern phys_addr_t read_cr3(void);
extern void        write_cr3(phys_addr_t phys_addr);
extern void        __invlpg(virt_addr_t address);
extern uint8_t     has_pml5(void);
extern uint8_t     has_nx(void);
extern void        enable_pml5();
extern void        enable_nx();
extern void        enable_wp();
extern virt_addr_t read_cr2();
extern void        write_cr2(virt_addr_t cr2);
extern void        __wbinvd();
/* locals */
static pagemgr_root_t page_manager = {0};
static pagemgr_t      pagemgr_if   = {0};
static physmm_t       *physmm      = NULL;

static virt_addr_t pagemgr_temp_map(phys_addr_t phys, uint16_t ix);
static int         pagemgr_temp_unmap(virt_addr_t vaddr);
static virt_addr_t pagemgr_alloc(virt_addr_t virt, virt_size_t length, uint32_t attr);
static virt_addr_t pagemgr_map(virt_addr_t virt, phys_addr_t phys, virt_size_t length, uint32_t attr);
static int         pagemgr_page_fault_handler(void *pv, phys_size_t error_code);
static int         pagemgr_attr_change(virt_addr_t vaddr, virt_size_t len, uint32_t attr);
static int         pagemgr_free(virt_addr_t vaddr, virt_size_t len);
static int         pagemgr_unmap(virt_addr_t vaddr, virt_size_t len);

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


/*
 * This routine maps 8KB of memory from the 
 * page table created by the bootstrap.
 * After the page table is changed, this routine will
 * no longer work.
 */
virt_addr_t pagemgr_boot_temp_map(phys_addr_t phys_addr)
{
    uint16_t   offset = 0;
    virt_addr_t   virt_addr = 0;
    virt_addr_t   page_table = 0;
    virt_addr_t  *page = NULL;
      
    offset = phys_addr % PAGE_SIZE;
    phys_addr -= offset;

    if(page_manager.remap_table_vaddr == 0)
    {
         page_table = (virt_addr_t)&BOOT_PAGING;
         virt_addr =  (virt_addr_t)&KERNEL_VMA   |
                       PDT_INDEX_TO_VIRT(511)    |
                       PT_INDEX_TO_VIRT(510);

         page = (virt_addr_t*)(page_table + 0x3000 + 511 * PAGE_SIZE + 510 * 8);

        /* mark the page as present and writeable */
        page[0] =  phys_addr            | 0x1B;
        page[1] =  phys_addr + 0x1000   | 0x1B;

        __invlpg(virt_addr);
        __invlpg(virt_addr + PAGE_SIZE);
    }
    else
    {
       virt_addr = pagemgr_temp_map(phys_addr, 510);
       pagemgr_temp_map(phys_addr + PAGE_SIZE, 511);
    }
  
    return(virt_addr + offset);
}



static phys_size_t pagemgr_boot_alloc_cb(phys_addr_t phys, phys_size_t count, void *pv)
{
    phys_addr_t *pf = (phys_addr_t*)pv;
    
    if(*pf != 0)
        return(0);
    
    *pf = phys;

    return(1);
}

static phys_addr_t pagemgr_boot_alloc_pf()
{
    phys_addr_t pf = 0;

    physmm->alloc(1, 0, pagemgr_boot_alloc_cb, &pf);

    return(pf);
}

static int pagemgr_attr_translate(pte_bits_t *pte, uint32_t attr)
{
    if(pte->fields.present)
    {
        pte->fields.read_write      = !!(attr & PAGE_WRITABLE);
        pte->fields.user_supervisor = !!(attr & PAGE_USER);
        pte->fields.write_through   = !!(attr & PAGE_WRITE_THROUGH);
        pte->fields.cache_disable   = !!(attr & PAGE_NO_CACHE);
        pte->fields.xd              =  !(attr & PAGE_EXECUTABLE);
        return(0);
    }

    return(-1);
}

static int pagemgr_build_init_pagetable(void)
{
    pml5e_bits_t pml5e;
    pml4e_bits_t pml4e;
    pdpte_bits_t pdpte;
    pde_bits_t   pde;
    pte_bits_t   pte;
    phys_addr_t  paddr        = (uint64_t)&BOOTSTRAP_END;
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
    uint8_t      pml5_support = page_manager.pml5_support;

    /* setup the top most page table */    
    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
    memset(work_ptr, 0, PAGE_SIZE);


    for(uint8_t step = 0; step < 2; step++)
    {
        if(step == PAGE_KERNEL_MAP_STEP)
        {
            vbase     = (phys_addr_t)&KERNEL_VMA + paddr;
            req_len = (phys_addr_t)&KERNEL_VMA_END - vbase;
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
                    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(PAGE_MASK_ADDRESS(page_manager.page_phys_base));
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
                    work_ptr = (virt_addr_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);

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
            if(step ==PAGE_KERNEL_MAP_STEP)
            {
                pte.bits = paddr;

                pte.fields.xd = page_manager.do_nx & 0x1;
        
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

                paddr+=PAGE_SIZE;
                
            }
            /* create the remapping table (last 2MB) */
            else if(step == PAGE_REMAP_STEP)
            {
                pte.fields.xd = page_manager.do_nx & 0x1;
                
                /* Set the first entry of the page
                 * table to point to the page table itself 
                 */

                if(pte_ix == 0)
                {
                    pte.bits = pde.bits;
                    pte.fields.read_write = 1;
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
 
    return(0);
}

int pagemgr_init(void)
{
    kprintf("Initializing Page Manager\n");
    physmm = physmm_get();
    memset(&page_manager, 0, sizeof(pagemgr_t));
    spinlock_init(&page_manager.lock);
    
    page_manager.page_phys_base = pagemgr_boot_alloc_pf();

    /* Check if we support No-Execute and if we do,
     * enable it 
     */ 
    if(has_nx())
    {
        enable_nx();
        page_manager.do_nx = 1;
    }

    /* Check PML5 support */

    if(has_pml5())
    {
        enable_pml5();
        page_manager.pml5_support = 1;
    }

    /* enable write protection -  
     * do not allow the kernel to write to pages that
     * are marked as read-only
     * This allows us to detect any attempt to write to read-only pages
     * since trying to do this will trigger a GPF
     */

    enable_wp();

    if(page_manager.page_phys_base == 0)
        return(-1);
    
    if(pagemgr_build_init_pagetable() == -1)
        return(-1);
 
    write_cr3(page_manager.page_phys_base);
    
    memset(&pagemgr_if, 0, sizeof(pagemgr_t));

    pagemgr_if.alloc    = pagemgr_alloc;
    pagemgr_if.map      = pagemgr_map;
    pagemgr_if.attr     = pagemgr_attr_change;
    pagemgr_if.dealloc  = pagemgr_free;
    pagemgr_if.unmap    = pagemgr_unmap;

    return(0);
}

int pagemgr_install_handler(void)
{
    return(isr_install(pagemgr_page_fault_handler, &page_manager, 14));
}

static virt_addr_t pagemgr_temp_map(phys_addr_t phys, uint16_t ix)
{
    virt_addr_t *remap_tbl = (virt_addr_t*)REMAP_TABLE_VADDR;
    virt_addr_t remap_value = 0;
    pte_bits_t pte = {0};

    phys = PAGE_MASK_ADDRESS(phys);
   
    if(phys % PAGE_SIZE)
    {
        kprintf("DIED @ %d\n",__LINE__);
    
        return(0);
    }
    /* do not allow ix to be 0 (which is the root of the table) 
     * or above 511 (which is above the page table)
     */

    if(ix == 0 || ix > 511)
    {
        kprintf("DIED @ %d\n",__LINE__);
        return(0);
    }
    
    pte.bits = phys;
    pte.fields.read_write = 1;
    pte.fields.present    = 1;

    remap_tbl[ix] = pte.bits;
    
    remap_value = REMAP_TABLE_VADDR + (PAGE_SIZE * ix);

    
    __invlpg(remap_value);

    return(remap_value);
}

static int pagemgr_temp_unmap(virt_addr_t vaddr)
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

}

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


                //kprintf("path->pd[path->pdt_ix].bits 0x%x\n",path->pd[path->pdt_ix].bits);

                if(clear)
                {
                    memset(path->pt, 0, PAGE_SIZE);
                    clear = 0;
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

static phys_size_t pagemgr_alloc_or_map_cb(phys_addr_t phys, phys_size_t count, void *pv)
{
    pagemgr_path_t *path = (pagemgr_path_t*)pv;
    phys_size_t used_pf     = 0;
    virt_addr_t virt        = 0;
    
    while(used_pf < count && path->virt_off < path->req_len)
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
       
        path->pt_ix = VIRT_TO_PT_INDEX(virt);
        
        if(path->pt[path->pt_ix].fields.present == 0)
        {
            path->pt[path->pt_ix].bits = phys + used_pf * PAGE_SIZE;

            path->pt[path->pt_ix].fields.present = 1;
        }
        
        /* Any attempt to apply attributes to a non-existent page
         * will cause the function to exit 
         */
        
        if(pagemgr_attr_translate(&path->pt[path->pt_ix], path->attr))
        {
            return(0);
        }
        pagemgr_invalidate(virt);
        path->virt_off += PAGE_SIZE;
        used_pf++;
    }
    
    return(used_pf);
}

static int pagemgr_free_or_unmap_cb(phys_addr_t *phys, phys_size_t *count, void *pv)
{
    pagemgr_path_t *path      = (pagemgr_path_t*)pv;
    phys_addr_t     start_phys  = 0;
    phys_size_t     page_count  = 0;
    phys_addr_t     page_phys   = 0;
    virt_addr_t     virt        = 0;

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
  
  //  if(path->req_len != 4096ull*1024ull*1024ull)
              //  kprintf("REL 0x%x -> 0x%x\n",virt, PAGE_MASK_ADDRESS(path->pt[path->pt_ix].bits));
       
        
#if 1
        if(path->pt[path->pt_ix].fields.present)
        {
            /* No pages in counter - set up starting address */
            if(page_count == 0)
            {
                start_phys = page_phys;
                page_count = 1;
                path->pt[path->pt_ix].bits = 0;

                /* Make sure that this entry is flushed from TLB */
                pagemgr_invalidate(virt);
                path->virt_off += PAGE_SIZE;
                continue;
            }
            /* See if this page is in chain with start_phys */
            else if(page_phys == start_phys + page_count * PAGE_SIZE)
            {
                page_count++;
                path->pt[path->pt_ix].bits = 0;
                /* Make sure that this entry is flushed from TLB */
                pagemgr_invalidate(virt);

                path->virt_off += PAGE_SIZE;
                continue;
            }
            /* Current page is not in the chain - let's sent
             * what we got so far to the PHYSMM
             */ 
            else
            {
                *phys = start_phys;
                *count = page_count;
                return(1);
            }
        }
        else
        {
            kprintf("STOP: VIRT 0x%x - BITS 0x%x\n", virt,path->pt[path->pt_ix].bits );
            while(1);
        }

#else
        if(path->pt[path->pt_ix].bits)
        {
                start_phys = PAGE_MASK_ADDRESS(path->pt[path->pt_ix].bits);
                page_count =1;
                path->pt[path->pt_ix].bits = 0;
                
                *phys = start_phys;
                *count = page_count;

                pagemgr_invalidate(virt);
                return(1);
        }
#endif

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

    virt_off = path->virt_off;
    PAGE_PATH_RESET(path);
    /* Restore the offset */
    path->virt_off = virt_off;

    if(ret == 0)
    {   
        return(0);
    }
   
    /* 
     * TODO: Preserve the state of path checking to improve
     * path building 
     */
    /* save the offset before reseting the path */

    ret = physmm->alloc(0, ALLOC_CB_STOP, pagemgr_alloc_pages_cb, (void*)path);

    if(ret == -1 || path->req_len > path->virt_off)
    {
        return(-1);
    }

    PAGE_PATH_RESET(path);

    return(0);
}

static virt_addr_t pagemgr_map(virt_addr_t virt, phys_addr_t phys, virt_size_t length, uint32_t attr)
{
    pagemgr_path_t path;
    int            ret = 0;
    phys_size_t    pg_frames = 0;

    memset(&path, 0, sizeof(pagemgr_path_t));

    path.virt     = virt;
    path.req_len  = length;
    path.attr     = attr;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);

    spinlock_lock_interrupt(&page_manager.lock);

    if(pagemgr_build_page_path(&path) < 0)
    {
        spinlock_unlock_interrupt(&page_manager.lock);
        return(0);
    }
    
    PAGE_PATH_RESET(&path);

    pg_frames     = length / PAGE_SIZE;

    pagemgr_alloc_or_map_cb(phys, pg_frames, &path);
    
    spinlock_unlock_interrupt(&page_manager.lock);
    
    return(virt);
}

static virt_addr_t pagemgr_alloc(virt_addr_t virt, virt_size_t length, uint32_t attr)
{
    pagemgr_path_t path;
    phys_addr_t    pg_frames = 0;
    int            ret = 0;
 
    memset(&path, 0, sizeof(pagemgr_path_t));
 
    path.virt     = virt;
    path.req_len  = length;
    path.attr     = attr;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    
    spinlock_lock_interrupt(&page_manager.lock);

    ret = pagemgr_build_page_path(&path);

    if(ret < 0)
    {
        spinlock_unlock_interrupt(&page_manager.lock);
        return(0);
    }   

    PAGE_PATH_RESET(&path);

    pg_frames = length / PAGE_SIZE;
    
    ret = physmm->alloc(pg_frames, 0, (alloc_cb)pagemgr_alloc_or_map_cb, (void*)&path);    
    
    if(ret < 0)
    {
        kprintf("NO MORE BMP\n");
        spinlock_unlock_interrupt(&page_manager.lock);
        pagemgr_free(virt, path.virt_off);
        return(0);
    }

    spinlock_unlock_interrupt(&page_manager.lock);
#if 0
    if(length!=4096ull*1024ull*1024ull)
        pagemgr_verify_pages(virt,length);
#endif
    return(virt);
}

static int pagemgr_attr_change(virt_addr_t vaddr, virt_size_t len, uint32_t attr)
{
    pagemgr_path_t path;
    phys_addr_t    pg_frames = 0;
    phys_addr_t    ret       = 0;

    memset(&path, 0, sizeof(pagemgr_path_t));
    
    path.virt     = vaddr;
    path.req_len  = len;
    path.attr     = attr;
    path.check = 1;
    pg_frames = len / PAGE_SIZE;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);

    PAGE_PATH_RESET(&path);
    
    spinlock_lock_interrupt(&page_manager.lock);
    
    ret = pagemgr_alloc_or_map_cb(0, pg_frames, &path) == pg_frames;
    ret = ret ? 0 : -1;

    spinlock_unlock_interrupt(&page_manager.lock);

    return((int)ret);
}

static int pagemgr_free(virt_addr_t vaddr, virt_size_t len)
{
    int ret = 0;
    pagemgr_path_t path;

    memset(&path,0,sizeof(path));
    
    path.virt = vaddr;
    path.req_len = len;
    
    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    
    PAGE_PATH_RESET(&path);
    
    spinlock_lock_interrupt(&page_manager.lock);

    ret = pagemgr_check_page_path(&path);

    if(ret != 0)
    {
        spinlock_unlock_interrupt(&page_manager.lock);
        return(-1);
    }

    PAGE_PATH_RESET(&path);
    
    physmm->dealloc(pagemgr_free_or_unmap_cb, &path);

#if 0
    if(stop)
    {
        kprintf("---------------MEMORY CORRUPTED---------------------\n");
        physmm_dump_bitmaps();

        while(1);
    }
#endif
    spinlock_unlock_interrupt(&page_manager.lock);

    return(0);
}

static int pagemgr_unmap(virt_addr_t vaddr, virt_size_t len)
{
    int ret = 0;
    pagemgr_path_t path;
    phys_addr_t dummy_phys  = 0;
    phys_size_t dummy_count = 0;
    memset(&path,0,sizeof(path));

    path.virt    = vaddr;
    path.req_len = len;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    spinlock_lock_interrupt(&page_manager.lock);

    PAGE_PATH_RESET(&path);
    
    ret = pagemgr_check_page_path(&path);
    
    if(ret != 0)
    {
        spinlock_unlock_interrupt(&page_manager.lock);
        return(-1);
    }
    PAGE_PATH_RESET(&path);

    while(pagemgr_free_or_unmap_cb(&dummy_phys, &dummy_count, &path));

    spinlock_unlock_interrupt(&page_manager.lock);
    
    return(0);
}

void pagemgr_verify_pages(virt_addr_t v, virt_size_t len)
{
    pagemgr_path_t p;
    memset(&p, 0, sizeof(pagemgr_path_t));

    p.virt = v;
    p.req_len = len;
    p.check = 1;
    if(page_manager.pml5_support)
        p.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        p.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    PAGE_PATH_RESET(&p);
    
    pagemgr_check_page_path(&p);
    return(0);
}


pagemgr_t * pagemgr_get(void)
{
    return(&pagemgr_if);
}

static int pagemgr_page_fault_handler(void *pv, virt_size_t error_code)
{
    virt_addr_t fault_address = read_cr2();
    
    kprintf("ADDRESS 0x%x ERROR 0x%x\n",fault_address, error_code);

    return(0);
}