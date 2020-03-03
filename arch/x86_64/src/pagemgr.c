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
}pagemgr_t;

/* defines */

#define PAGE_KERNEL_MAP_STEP (0x0)
#define PAGE_REMAP_STEP      (0x1)

#define PAGE_TEMP_REMAP_PML5 (0x1)
#define PAGE_TEMP_REMAP_PML4 (0x2)
#define PAGE_TEMP_REMAP_PDPT (0x3)
#define PAGE_TEMP_REMAP_PDT  (0x4)
#define PAGE_TEMP_REMAP_PT   (0x5)

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

/* locals */
static pagemgr_t page_manager = {0};
static uint64_t pagemgr_temp_map(uint64_t phys, uint16_t ix);
static int pagemgr_temp_unmap(uint64_t vaddr);



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
                       PDE_INDEX_TO_VIRT(511)    |
                       PTE_INDEX_TO_VIRT(510);
         page = (uint64_t*)(page_table + 0x3000 + 511 * 4096 + 510 * 8);

        /* mark the page as present and writeable */
        page[0] =  phys_addr            | 0x3;
        page[1] =  phys_addr + 0x1000   | 0x3;

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
extern uint64_t KERNEL_VMA_END;

uint64_t pagemgr_early_alloc(uint64_t vaddr, uint64_t len, uint16_t attr);

int pagemgr_build_init_pagetable(void)
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
                    pml5e.bits = physmm_early_alloc_pf();
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
                pml4e.bits = physmm_early_alloc_pf();
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

            if(pdpte_ix != VIRT_TO_PDPTE_INDEX(vaddr))
            {
                pdpte_ix = VIRT_TO_PDPTE_INDEX(vaddr);
                pdpte.bits = physmm_early_alloc_pf();
            
                pdpte.fields.present = 1;
                pdpte.fields.read_write = 1;

                /* Prepare PDT */

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits & ~(ATTRIBUTE_MASK));
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* Update ENTRY in PDPT */

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits & (~ATTRIBUTE_MASK));
                memcpy(&work_ptr[pdpte_ix], &pdpte, sizeof(uint64_t));
            }

            if(pde_ix != VIRT_TO_PDE_INDEX(vaddr))
            {
                pde_ix = VIRT_TO_PDE_INDEX(vaddr);
                pde.bits = physmm_early_alloc_pf();
                pde.fields.present = 1;
                pde.fields.read_write = 1;
            
                /* Prepare PT */

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pde.bits & ~(ATTRIBUTE_MASK));
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* Update ENTRY in PDT */

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits & (~ATTRIBUTE_MASK));
                memcpy(&work_ptr[pde_ix], &pde, sizeof(uint64_t));
            }

            pte_ix = VIRT_TO_PTE_INDEX(vaddr);

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

    page_manager.pml5_support = has_pml5();

    memset(&page_manager, 0, sizeof(pagemgr_t));

    page_manager.page_phys_base = physmm_early_alloc_pf();

    if(page_manager.page_phys_base == 0)
        return(-1);
    
    if(pagemgr_build_init_pagetable() == -1)
        return(-1);

    write_cr3(page_manager.page_phys_base);
    
    return(0);
}

static uint64_t pagemgr_temp_map(uint64_t phys, uint16_t ix)
{
    uint64_t *remap_tbl = (uint64_t*)REMAP_TABLE_VADDR;
    uint64_t remap_value = 0;
    pte_bits_t pte = {0};

    if(phys % PAGE_SIZE)
    {
        kprintf("DIED\n");
        return(0);
    }
    /* do not allow ix to be 0 (which is the root of the table) 
     * or above 511 (which is above the page table)
     */

    if(ix == 0 || ix > 511)
    {
        kprintf("DIED\n");
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

uint64_t pagemgr_early_alloc(uint64_t vaddr, uint64_t len, uint16_t attr)
{
    pml5e_bits_t *pml5    = NULL;
    pml4e_bits_t *pml4    = NULL;
    pdpte_bits_t *pdpt   = NULL;
    pde_bits_t   *pdt     = NULL;
    pte_bits_t   *pt     = NULL;

    uint16_t      pml5_ix = -1;
    uint16_t      pml4_ix = -1;
    uint16_t      pdpt_ix = -1;
    uint16_t      pde_ix  = -1;
    uint16_t      pte_ix  = -1;
    uint64_t      vaddr_end = vaddr + len;
    uint64_t      ret = vaddr;

    if(page_manager.pml5_support)
        pml5 = (pml5e_bits_t*)pagemgr_temp_map(page_manager.page_phys_base, 
                                               PAGE_TEMP_REMAP_PML5);
    else
        pml4 = (pml4e_bits_t*)pagemgr_temp_map(page_manager.page_phys_base, 
                                               PAGE_TEMP_REMAP_PML4);
    
    while(vaddr < vaddr_end)
    {
        if(page_manager.pml5_support)
        {
            if(pml5_ix != VIRT_TO_PML5_INDEX(vaddr))
            {
                pml5_ix = VIRT_TO_PML5_INDEX(vaddr);

                if(pml5[pml5_ix].fields.present == 0)
                {
                    pml5[pml5_ix].bits = physmm_early_alloc_pf();
                    pml5[pml5_ix].fields.present = 1;
                    pml5[pml5_ix].fields.read_write = 1;
                       
                }

                pml4 = (pml4e_bits_t*)pagemgr_temp_map(pml5[pml5_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PML4);
            }
        }
        
        if(pml4_ix != VIRT_TO_PML4_INDEX(vaddr))
        {
            pml4_ix = VIRT_TO_PML4_INDEX(vaddr);

            if(pml4[pml4_ix].fields.present == 0)
            {
                pml4[pml4_ix].bits = physmm_early_alloc_pf();
                pml4[pml4_ix].fields.present = 1;
                pml4[pml4_ix].fields.read_write = 1;
            }
       
            pdpt = (pdpte_bits_t*)pagemgr_temp_map(pml4[pml4_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PDPT);
        }

        if(pdpt_ix != VIRT_TO_PDPTE_INDEX(vaddr))
        {
            
            pdpt_ix = VIRT_TO_PDPTE_INDEX(vaddr);

            if(pdpt[pdpt_ix].fields.present == 0)
            {
                pdpt[pdpt_ix].bits = physmm_early_alloc_pf();
                pdpt[pdpt_ix].fields.present = 1;
                pdpt[pdpt_ix].fields.read_write = 1;
            }

            pdt = (pde_bits_t*)pagemgr_temp_map(pdpt[pdpt_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PDT);
        }

        if(pde_ix != VIRT_TO_PDE_INDEX(vaddr))
        {
            pde_ix = VIRT_TO_PDE_INDEX(vaddr);

            if(pdt[pde_ix].fields.present == 0)
            {
                pdt[pde_ix].bits = physmm_early_alloc_pf();
                pdt[pde_ix].fields.present = 1;
                pdt[pde_ix].fields.read_write = 1;
            }

            pt =(pte_bits_t*) pagemgr_temp_map(pdt[pde_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PT);
        }
        pte_ix = VIRT_TO_PTE_INDEX(vaddr);

        pt[pte_ix].bits = physmm_early_alloc_pf();
        pt[pte_ix].fields.read_write = 1;
        pt[pte_ix].fields.present = 1;

        vaddr += 0x1000;
    }

    return(ret);
}

uint64_t pagemgr_early_map(uint64_t vaddr, uint64_t paddr, uint64_t len, uint16_t attr)
{
    pml5e_bits_t *pml5    = NULL;
    pml4e_bits_t *pml4    = NULL;
    pdpte_bits_t *pdpt   = NULL;
    pde_bits_t   *pdt     = NULL;
    pte_bits_t   *pt     = NULL;

    uint16_t      pml5_ix = -1;
    uint16_t      pml4_ix = -1;
    uint16_t      pdpt_ix = -1;
    uint16_t      pde_ix  = -1;
    uint16_t      pte_ix  = -1;
    
    uint64_t      vaddr_end = vaddr + len;
    uint64_t      ret = vaddr;

    if(page_manager.pml5_support)
        pml5 = (pml5e_bits_t*)pagemgr_temp_map(page_manager.page_phys_base, 
                                               PAGE_TEMP_REMAP_PML5);
    else
        pml4 = (pml4e_bits_t*)pagemgr_temp_map(page_manager.page_phys_base, 
                                               PAGE_TEMP_REMAP_PML4);
    
    while(vaddr < vaddr_end)
    {
        if(page_manager.pml5_support)
        {
            if(pml5_ix != VIRT_TO_PML5_INDEX(vaddr))
            {
                pml5_ix = VIRT_TO_PML5_INDEX(vaddr);

                if(pml5[pml5_ix].fields.present == 0)
                {
                    pml5[pml5_ix].bits = physmm_early_alloc_pf();
                    pml5[pml5_ix].fields.present = 1;
                    pml5[pml5_ix].fields.read_write = 1;
                }

                pml4 = (pml4e_bits_t*)pagemgr_temp_map(pml5[pml5_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PML4);
            }
        }
        
        if(pml4_ix != VIRT_TO_PML4_INDEX(vaddr))
        {
            pml4_ix = VIRT_TO_PML4_INDEX(vaddr);

            if(pml4[pml4_ix].fields.present == 0)
            {
                pml4[pml4_ix].bits = physmm_early_alloc_pf();
                pml4[pml4_ix].fields.present = 1;
                pml4[pml4_ix].fields.read_write = 1;
            }
       
            pdpt = (pdpte_bits_t*)pagemgr_temp_map(pml4[pml4_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PDPT);
        }

        if(pdpt_ix != VIRT_TO_PDPTE_INDEX(vaddr))
        {
            
            pdpt_ix = VIRT_TO_PDPTE_INDEX(vaddr);

            if(pdpt[pdpt_ix].fields.present == 0)
            {
                pdpt[pdpt_ix].bits = physmm_early_alloc_pf();
                pdpt[pdpt_ix].fields.present = 1;
                pdpt[pdpt_ix].fields.read_write = 1;
            }

            pdt = (pde_bits_t*)pagemgr_temp_map(pdpt[pdpt_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PDT);
        }

        if(pde_ix != VIRT_TO_PDE_INDEX(vaddr))
        {
            pde_ix = VIRT_TO_PDE_INDEX(vaddr);

            if(pdt[pde_ix].fields.present == 0)
            {
                pdt[pde_ix].bits = physmm_early_alloc_pf();
                pdt[pde_ix].fields.present = 1;
                pdt[pde_ix].fields.read_write = 1;
            }

            pt =(pte_bits_t*) pagemgr_temp_map(pdt[pde_ix].bits & (~ATTRIBUTE_MASK),
                                                PAGE_TEMP_REMAP_PT);
        }
        pte_ix = VIRT_TO_PTE_INDEX(vaddr);

        pt[pte_ix].bits = paddr;
        pt[pte_ix].fields.read_write = 1;
        pt[pte_ix].fields.present = 1;

        vaddr += 0x1000;
        paddr += 0x1000;
    }
    return(ret);
}
