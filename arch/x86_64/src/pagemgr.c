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
typedef struct 
{
    uint64_t page_phys_base; /* physical location of the first
                              * level of paging
                              */ 
    uint64_t remap_table_vaddr;
    uint8_t  pml5_support;
    uint8_t  do_nx;
}pagemgr_root_t;

typedef struct
{
    uint16_t      pml5_ix;
    uint16_t      pml4_ix;
    uint16_t      pdpt_ix;
    uint16_t      pdt_ix ;
    uint16_t      pt_ix  ;
    uint64_t      virt;
    uint64_t      virt_end;
    pml5e_bits_t *pml5;
    pml4e_bits_t *pml4;
    pdpte_bits_t *pdpt;
    pde_bits_t   *pd;
    pte_bits_t   *pt;
    uint32_t      attr;
    uint64_t      page_phys_base;
}pagemgr_path_t;


/* defines */

#define PAGE_KERNEL_MAP_STEP (0x0)
#define PAGE_REMAP_STEP      (0x1)

#define PAGE_TEMP_REMAP_PML5 (0x1)
#define PAGE_TEMP_REMAP_PML4 (0x2)
#define PAGE_TEMP_REMAP_PDPT (0x3)
#define PAGE_TEMP_REMAP_PDT  (0x4)
#define PAGE_TEMP_REMAP_PT   (0x5)

#define PAGE_MASK_ADDRESS(x) ((x) & ~(ATTRIBUTE_MASK))
#define PAGE_STRUCT_TEMP_MAP(x,y) pagemgr_temp_map(PAGE_MASK_ADDRESS((x)), (y))
#define PAGE_STRUCT_TEMP_UNMAP(x) pagemgr_temp_unmap(PAGE_MASK_ADDRESS((x)))

/* externs */
extern uint64_t BOOT_PAGING;
extern uint64_t KERNEL_VMA;
extern uint64_t KERNEL_VMA_END;
extern uint64_t BOOTSTRAP_END;
extern uint64_t _code;
extern uint64_t _code_end;
extern uint64_t _data;
extern uint64_t _data_end;
extern uint64_t _rodata;
extern uint64_t _rodata_end;
extern uint64_t _bss;
extern uint64_t _bss_end;

extern uint64_t read_cr3(void);
extern uint64_t write_cr3(uint64_t phys_addr);
extern void     __invlpg(uint64_t address);
extern uint8_t  has_pml5(void);
extern uint8_t  has_nx(void);
extern void     enable_pml5();
extern void     enable_nx();
extern void     enable_wp();

/* locals */
static pagemgr_root_t page_manager = {0};
static pagemgr_t      pagemgr_if   = {0};
static physmm_t       *physmm      = NULL;

static uint64_t pagemgr_temp_map(uint64_t phys, uint16_t ix);
static int pagemgr_temp_unmap(uint64_t vaddr);
static uint64_t pagemgr_alloc(uint64_t virt, uint64_t length, uint32_t attr);
static uint64_t pagemgr_map(uint64_t virt, uint64_t phys, uint64_t length, uint32_t attr);

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
uint64_t pagemgr_boot_temp_map(uint64_t phys_addr)
{
    uint16_t   offset = 0;
    uint64_t   virt_addr = 0;
    uint64_t   page_table = 0;
    uint64_t  *page = NULL;
      
    offset = phys_addr % PAGE_SIZE;
    phys_addr -= offset;

    if(page_manager.remap_table_vaddr == 0)
    {
         page_table = (uint64_t)&BOOT_PAGING;
         virt_addr =  (uint64_t)&KERNEL_VMA      |
                       PDT_INDEX_TO_VIRT(511)    |
                       PT_INDEX_TO_VIRT(510);
         page = (uint64_t*)(page_table + 0x3000 + 511 * 4096 + 510 * 8);

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

static uint8_t pagemgr_boot_alloc_cb(uint64_t phys, uint64_t count, void *pv)
{
    uint64_t *pf = (uint64_t*)pv;
    
    if(*pf != 0)
        return(0);
    
    *pf = phys;

    return(1);
}

static uint64_t pagemgr_boot_alloc_pf()
{
    uint64_t pf = 0;
    physmm->alloc(1, 0, pagemgr_boot_alloc_cb, &pf);
    return(pf);
}

static void pagemgr_attr_translate(pte_bits_t *pte, uint32_t attr)
{
    pte->fields.read_write      = !!(attr & PAGE_WRITABLE);
    pte->fields.user_supervisor = !!(attr & PAGE_USER);
    pte->fields.write_through   = !!(attr & PAGE_WRITE_THROUGH);
    pte->fields.cache_disable   = !!(attr & PAGE_NO_CACHE);
    pte->fields.xd              =  !(attr & PAGE_EXECUTABLE);

}

static int pagemgr_build_init_pagetable(void)
{
    pml5e_bits_t pml5e;
    pml4e_bits_t pml4e;
    pdpte_bits_t pdpte;
    pde_bits_t   pde;
    pte_bits_t   pte;
    uint64_t     paddr        = (uint64_t)&BOOTSTRAP_END;
    uint64_t     vaddr        = 0;
    uint64_t     vaddr_end    = 0;
    uint64_t     *work_ptr    = 0;
    uint64_t     phys_addr    = 0;
    uint16_t     pml5e_ix     = -1;
    uint16_t     pml4e_ix     = -1;
    uint16_t     pdpte_ix     = -1;
    uint16_t     pde_ix       = -1;
    uint16_t     pte_ix       = -1;
    uint8_t      krnl_tbl_ok  = 0;
    uint8_t      pml5_support = page_manager.pml5_support;
    
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

    /* setup the top most page table */    
    work_ptr = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
    memset(work_ptr, 0, PAGE_SIZE);


    for(uint8_t step = 0; step < 2; step++)
    {
        if(step == PAGE_KERNEL_MAP_STEP)
        {
            vaddr     = (uint64_t)&KERNEL_VMA + paddr;
            vaddr_end = (uint64_t)&KERNEL_VMA_END;
        }
        else
        {
            vaddr = REMAP_TABLE_VADDR;
            vaddr_end = (uint64_t)-1;
        }

        while(vaddr < vaddr_end && vaddr != 0)
        {
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
                    work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml5e.bits & ~(ATTRIBUTE_MASK));
                    memset(work_ptr, 0, PAGE_SIZE);

                    /* Update ENTRY in PML5 */
                    work_ptr = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
                    memcpy(&work_ptr[pml5e_ix], &pml5e, sizeof(uint64_t));
                
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

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits & ~(ATTRIBUTE_MASK));
                memset(work_ptr, 0, PAGE_SIZE);
                        
                /* Update ENTRY in PML4 */
                if(pml5_support)
                    work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml5e.bits & (~ATTRIBUTE_MASK));
                else
                    work_ptr = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);

                memcpy(&work_ptr[pml4e_ix], &pml4e, sizeof(uint64_t));
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

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits & ~(ATTRIBUTE_MASK));
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* Update ENTRY in PDPT */

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits & (~ATTRIBUTE_MASK));
                memcpy(&work_ptr[pdpte_ix], &pdpte, sizeof(uint64_t));
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

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pde.bits & ~(ATTRIBUTE_MASK));
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* Update ENTRY in PDT */

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits & (~ATTRIBUTE_MASK));
                memcpy(&work_ptr[pde_ix], &pde, sizeof(uint64_t));
            }

            pte_ix = VIRT_TO_PT_INDEX(vaddr);

            /* set up page tables for mapping the kernel image */            
            if(step ==PAGE_KERNEL_MAP_STEP)
            {
                pte.bits = paddr;

                pte.fields.xd = page_manager.do_nx & 0x1;
        
                /* code must be read-only and executable */
                if(vaddr >= (uint64_t)&_code && vaddr <=(uint64_t)&_code_end)
                {
                    pte.fields.read_write = 0;
                    pte.fields.xd         = 0;
                }
                /* data and bss must be read/write */
                else if((vaddr >= (uint64_t)&_data && vaddr <= (uint64_t)&_data_end) ||
                        (vaddr >= (uint64_t)&_bss && vaddr <= (uint64_t)&_bss_end))
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
                 * table to point to the page table itself */

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

            vaddr+=PAGE_SIZE;

            work_ptr = (uint64_t*)pagemgr_boot_temp_map(pde.bits & (~ATTRIBUTE_MASK));
            memcpy(&work_ptr[pte_ix], &pte, sizeof(uint64_t));

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
    physmm = physmm_get();
    page_manager.pml5_support = has_pml5();

    memset(&page_manager, 0, sizeof(pagemgr_t));

    page_manager.page_phys_base = pagemgr_boot_alloc_pf();
    
    if(page_manager.page_phys_base == 0)
        return(-1);
    
    if(pagemgr_build_init_pagetable() == -1)
        return(-1);

    write_cr3(page_manager.page_phys_base);
    memset(&pagemgr_if, 0, sizeof(pagemgr_t));

    pagemgr_if.alloc = pagemgr_alloc;
    pagemgr_if.map = pagemgr_map;

    return(0);
}

static uint64_t pagemgr_temp_map(uint64_t phys, uint16_t ix)
{
    uint64_t *remap_tbl = (uint64_t*)REMAP_TABLE_VADDR;
    uint64_t remap_value = 0;
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

static int pagemgr_temp_unmap(uint64_t vaddr)
{
    uint16_t ix = 0;
    uint64_t *remap_tbl = (uint64_t*)REMAP_TABLE_VADDR;

    if(vaddr % PAGE_SIZE || vaddr <= REMAP_TABLE_VADDR)
        return(-1);

    ix = (vaddr - REMAP_TABLE_VADDR) / PAGE_SIZE;

    /* remove address */
    remap_tbl[ix] = 0;

    /* invalidate entry */
    __invlpg(vaddr);

    return(0);
}

static uint8_t pagemgr_alloc_pages_cb(uint64_t phys, uint64_t count, void *pv)
{
    uint8_t         used_pf = 0;
    uint8_t         clear   = 0;
    pagemgr_path_t *path    = (pagemgr_path_t*)pv;
    
    while((used_pf < count) && (path->virt < path->virt_end))
    {
        
        if(page_manager.pml5_support)
        {
            if(path->pml5_ix != VIRT_TO_PML5_INDEX(path->virt))
            {
                path->pml5_ix = VIRT_TO_PML5_INDEX(path->virt);
        
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
                    memset(path->pml4, 0, PAGE_SIZE);

                clear = 0;
            }
        }
        
        if(used_pf >= count)
            return(used_pf);

        if(path->pml4)
        {
            
            if(path->pml4_ix != VIRT_TO_PML4_INDEX(path->virt))
            {
                path->pml4_ix = VIRT_TO_PML4_INDEX(path->virt);
             
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
                }
            }
        }

        if(used_pf >= count)
            return(used_pf);
        
        if(path->pdpt)
        {
            if(path->pdpt_ix != VIRT_TO_PDPT_INDEX(path->virt))
            {
                path->pdpt_ix = VIRT_TO_PDPT_INDEX(path->virt);
             
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
                }
            }
        }

        if(used_pf >= count)
            return(used_pf);

        if(path->pd)
        {
            if(path->pdt_ix != VIRT_TO_PDT_INDEX(path->virt))
            {
                path->pdt_ix = VIRT_TO_PDT_INDEX(path->virt);
             
                if(path->pd[path->pdt_ix].fields.present == 0)
                {
                    path->pd[path->pdt_ix].bits = phys + used_pf * PAGE_SIZE;
                    path->pd[path->pdt_ix].fields.read_write = 1;
                    path->pd[path->pdt_ix].fields.present    = 1;
                    used_pf++;
                    clear = 1;
                }
                
                path->pt = (pte_bits_t*) PAGE_STRUCT_TEMP_MAP(path->pd[path->pdt_ix].bits,
                                               PAGE_TEMP_REMAP_PT);
                if(clear)
                {
                    memset(path->pt, 0, PAGE_SIZE);
                    clear = 0;
                }
            }
        }

        path->virt+=PAGE_SIZE;
    }

    return(used_pf);
}

/* 
 * pagemgr_build_page_path
 * --------------------------
 * Allocates pages used for address
 * translation
 */

static int pagemgr_build_page_path(pagemgr_path_t *path)
{
    int ret        = 0;

    path->pml5_ix  = ~0;
    path->pml4_ix  = ~0;
    path->pdpt_ix  = ~0;
    path->pdt_ix   = ~0;

    ret = physmm->alloc(0, ALLOC_CB_STOP, pagemgr_alloc_pages_cb, (void*)path);
    
    if(ret == -1 || path->virt < path->virt_end)
        return(-1);

    return(0);
}

static uint64_t pagemgr_alloc_or_map(uint64_t phys, uint64_t count, void *pv)
{
    pagemgr_path_t *path = (pagemgr_path_t*)pv;
    uint64_t used_pf = 0;

    while(used_pf < count && path->virt < path->virt_end)
    {
        if(page_manager.pml5_support)
        {
            if(path->pml5_ix != VIRT_TO_PML5_INDEX(path->virt))
            {
                path->pml5_ix = VIRT_TO_PML5_INDEX(path->virt);
                path->pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base,
                                                PAGE_TEMP_REMAP_PML5);

                path->pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml5[path->pml5_ix].bits,
                                                PAGE_TEMP_REMAP_PML4);
            }
        }

        if(path->pml4_ix != VIRT_TO_PML4_INDEX(path->virt))
        {
            path->pml4_ix = VIRT_TO_PML4_INDEX(path->virt);
        
            path->pdpt = (pdpte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pml4[path->pml4_ix].bits,
                                            PAGE_TEMP_REMAP_PDPT);
        }

        if(path->pdpt_ix != VIRT_TO_PDPT_INDEX(path->virt))
        {
            path->pdpt_ix = VIRT_TO_PDPT_INDEX(path->virt);

            path->pd = (pde_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pdpt[path->pdpt_ix].bits,
                                            PAGE_TEMP_REMAP_PDT);
                                         
        }

        if(path->pdt_ix != VIRT_TO_PDT_INDEX(path->virt))
        {
            path->pdt_ix = VIRT_TO_PDT_INDEX(path->virt);

            path->pt = (pte_bits_t*)PAGE_STRUCT_TEMP_MAP(path->pd[path->pdt_ix].bits,
                                            PAGE_TEMP_REMAP_PT);
        }

        path->pt_ix = VIRT_TO_PT_INDEX(path->virt);
        
        if(path->pt[path->pt_ix].fields.present == 0)
        {
            path->pt[path->pt_ix].bits = phys + used_pf * PAGE_SIZE;
            path->pt[path->pt_ix].fields.present = 1;
        }

        pagemgr_attr_translate(&path->pt[path->pt_ix], path->attr);
        
        path->virt += PAGE_SIZE;
        used_pf++;
    }
    
    return(used_pf);
}



static uint64_t pagemgr_map(uint64_t virt, uint64_t phys, uint64_t length, uint32_t attr)
{
    pagemgr_path_t path;
    int            ret = 0;
    uint64_t       pg_frames = 0;

    memset(&path, 0, sizeof(pagemgr_path_t));

    path.virt     = virt;
    path.virt_end = virt + length;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    
    if(pagemgr_build_page_path(&path) < 0)
        return(0);

    path.virt     = virt;
    path.pml5_ix  = ~0;
    path.pml4_ix  = ~0;
    path.pdpt_ix  = ~0;
    path.pdt_ix   = ~0;
    path.virt_end = virt + length;
    path.attr     = attr;

    pg_frames     = length / PAGE_SIZE;

    pagemgr_alloc_or_map(phys, pg_frames, &path);
 
    return(virt);
}

static uint64_t pagemgr_alloc(uint64_t virt, uint64_t length, uint32_t attr)
{
    pagemgr_path_t path;
    uint64_t       pg_frames = 0;
    int            ret = 0;

    memset(&path, 0, sizeof(pagemgr_path_t));

    path.virt     = virt;
    path.virt_end = virt + length;

    if(page_manager.pml5_support)
        path.pml5 = (pml5e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML5);
    else
        path.pml4 = (pml4e_bits_t*)PAGE_STRUCT_TEMP_MAP(page_manager.page_phys_base, 
                                                PAGE_TEMP_REMAP_PML4);
    if(pagemgr_build_page_path(&path) < 0)
        return(0);

    path.virt     = virt;
    path.pml5_ix  = ~0;
    path.pml4_ix  = ~0;
    path.pdpt_ix  = ~0;
    path.pdt_ix   = ~0;
    path.attr     = attr;
    
    pg_frames = length / PAGE_SIZE;

    ret = physmm->alloc(pg_frames, 0, (alloc_cb)pagemgr_alloc_or_map, (void*)&path);

    if(ret < 0)
        return(0);

    return(virt);
}

static int pagemgr_attr_change(uint64_t vaddr, uint64_t len, uint32_t attr)
{
    
}

pagemgr_t * pagemgr_get(void)
{
    return(&pagemgr_if);
}