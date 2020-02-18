/* Paging management 
 * This file contains code that would 
 * allow us to manage paging 
 * structures
 */

#include <physmm.h>
#include <stdint.h>
#include <paging.h>
#include <utils.h>
typedef struct 
{
    uint64_t page_phys_base; /* physical location of the first
                              * level of paging
                              */ 
    uint64_t temp_pf_phys_base;

}pagemgr_t;

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
extern uint64_t read_cr3();
extern uint64_t write_cr3(uint64_t phys_addr);
extern void     __invlpg(uint64_t address);

static pagemgr_t page_manager;

#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)

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
    uint16_t offset;
    uint64_t virt_addr =   (uint64_t)&KERNEL_VMA           |
                           PDE_INDEX_TO_VIRT(511)          |
                           PTE_INDEX_TO_VIRT(510);

    uint64_t boot_pt = (uint64_t)&BOOT_PAGING;
    uint64_t  *page = NULL;
      
    offset = phys_addr % PAGE_SIZE;
    phys_addr -= offset;

    /* Get the last two pages from the last 512 GB */
    /* the page table resides at boot_pt + 0x3000 
     * each page table takes 4KB so we will skip 511 page tables
     * to get to the last page table
     * after this we skip 510 entries so we get the starting address of the
     * last two pages.
     */

    page = (uint64_t*)(boot_pt + 0x3000 + 511 * 4096 + 510 * 8);

    /* mark the page as present and writeable */
    page[0] =  phys_addr            | 0x3;
    page[1] =  phys_addr + 0x1000   | 0x3;

    /* reload the page table */
    __invlpg(virt_addr);
    __invlpg(virt_addr + PAGE_SIZE);
  
    return(virt_addr + offset);
}
extern uint64_t KERNEL_VMA_END;


int pagemgr_init(void)
{
    pml4e_bits_t pml4e;
    pdpte_bits_t pdpte;
    pde_bits_t   pde;
    pte_bits_t   pte;
    uint64_t     paddr        = (uint64_t)&BOOTSTRAP_END;
    uint64_t     vaddr        = (uint64_t)&KERNEL_VMA+paddr;
    uint64_t     *work_ptr    = 0;
    uint64_t     phys_addr    = 0;
    uint16_t     pml4e_ix     = 0;
    uint16_t     pdpte_ix     = 0;
    uint16_t     pde_ix       = 0;
    uint16_t     pte_ix       = 0;
    uint8_t      remap_tbl_ok = 0;
    uint8_t      krnl_tbl_ok  = 0;

    memset(&page_manager, 0, sizeof(pagemgr_t));

    page_manager.page_phys_base = physmm_alloc_pf();
    
    if(page_manager.page_phys_base == 0)
        return(-1);
    
    /* clear the pml4 */

    
    work_ptr = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
    
    memset(work_ptr, 0, PAGE_SIZE);
    
    for(pml4e_ix = VIRT_TO_PML4_INDEX((uint64_t)vaddr); pml4e_ix < 512; pml4e_ix++)
    {
        /* Allocate PDPT */
        pml4e.bits = physmm_alloc_pf();
    
        if(pml4e.bits == 0)
            return(-1);
        
        /* Cleanup PDT */
        work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits);

        memset(work_ptr, 0, PAGE_SIZE);
    
        for(pdpte_ix = VIRT_TO_PDPTE_INDEX (vaddr); pdpte_ix < 512; pdpte_ix++)		 
        {
            /* allocate space for the PDE */
            pdpte.bits = physmm_alloc_pf();
        
            if(pdpte.bits == 0)
                return(-1);
        
            /* reserve and clean space of the PDE */
            work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits);
        
            memset(work_ptr, 0, PAGE_SIZE);
        
            for(pde_ix = VIRT_TO_PDE_INDEX(vaddr); pde_ix < 512; pde_ix++)
            {
            /* allocate space for the PT */
                pde.bits = physmm_alloc_pf();
             
                if(pde.bits == 0)
                    return(-1);
            
                /* Clear space of PT */
                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pde.bits);
                memset(work_ptr, 0, PAGE_SIZE);
            
                /* get the starting index from Page Table */

                for(pte_ix = VIRT_TO_PTE_INDEX(vaddr); pte_ix < 512; pte_ix++)
                {
                    pte.bits = paddr;
                    /* set default to present and non executable */
                    pte.fields.present = 0x1;
                  //  pte.fields.xd         = 1;

                    if(!krnl_tbl_ok)
                    {
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
                    
                        /* place entry in table */
                        memcpy(&work_ptr[pte_ix], &pte, sizeof(uint64_t));
                    }
                    
                    vaddr += PAGE_SIZE;
                    paddr += PAGE_SIZE;
                
                    if(vaddr >= (uint64_t)&KERNEL_VMA_END && !krnl_tbl_ok)
                    {
                        krnl_tbl_ok = 1;
                        break;
                    }
                }
            
                /* save the PDE */
                pde.fields.present = 1;
                pde.fields.read_write = 1;

                work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits);
                memcpy(&work_ptr[pde_ix], &pde, sizeof(uint64_t));
            
                if(krnl_tbl_ok == 1)
                    break;
            }
        
            /* set attributes of pdpte */
            pdpte.fields.present    = 0x1;
            pdpte.fields.read_write = 0x1;
        
            /* save PDPTE in memory */
            work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits);
            memcpy(&work_ptr[pdpte_ix], &pdpte , sizeof(uint64_t));
        
            /* if we're done with the kernel mapping, begin 
            * creating the temp mapping table
            */
        
            if(krnl_tbl_ok == 1)
                break;
        }
        
        pml4e.fields.present    = 0x1;
        pml4e.fields.read_write = 0x1;
    
        /* save the PML4 to memory */	
        work_ptr = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
        memcpy(&work_ptr[pml4e_ix], &pml4e, sizeof(uint64_t));
        
        if(krnl_tbl_ok == 1)
            break;
    }
    
    /* if we've overlapped the REMAP_TABLE_VADDR 
     *  just bail out
     */
    if(vaddr >= REMAP_TABLE_VADDR)
        return(-1);
    
    /* We did not overlap? Good. 
     * Let's build our last page table
     */

     if(VIRT_TO_PML4_INDEX(vaddr) != VIRT_TO_PML4_INDEX(REMAP_TABLE_VADDR))
     {
        pml4e.bits = physmm_alloc_pf();
        work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits);
        memset(work_ptr, 0, PAGE_SIZE);
     }
     else
     {
         kprintf("SAME PML4\n");
         pml4e.fields.present = 0;
         pml4e.fields.read_write = 0;
     }
     
     if(VIRT_TO_PDPTE_INDEX(vaddr) != VIRT_TO_PDPTE_INDEX(REMAP_TABLE_VADDR))
     {
        pdpte.bits = physmm_alloc_pf();
        work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits);
        memset(work_ptr, 0, PAGE_SIZE);
     }
     else
     {
         kprintf("SAME PDPTE\n");
         pdpte.fields.present = 0;
         pdpte.fields.read_write = 0;
     }
     
     if(VIRT_TO_PDE_INDEX(vaddr)  != VIRT_TO_PDE_INDEX(REMAP_TABLE_VADDR))
     {
        pde.bits = physmm_alloc_pf();
        work_ptr = (uint64_t*)pagemgr_boot_temp_map(pde.bits);
        memset(work_ptr, 0, PAGE_SIZE);
     }
     else
     {
         kprintf("SAME PDE\n");
         pde.fields.present = 0;
         pde.fields.read_write = 0;
     }
          
     pml4e_ix = VIRT_TO_PML4_INDEX(REMAP_TABLE_VADDR);
     pdpte_ix = VIRT_TO_PDPTE_INDEX(REMAP_TABLE_VADDR);
     pde_ix   = VIRT_TO_PDE_INDEX(REMAP_TABLE_VADDR);
     pte_ix   = VIRT_TO_PTE_INDEX(REMAP_TABLE_VADDR);

     
     pte.bits = pde.bits;
     //pte.fields.xd = 1;
     pte.fields.present = 1;
     pte.fields.read_write = 1;
     
     work_ptr = (uint64_t*)pagemgr_boot_temp_map(pde.bits);
     memcpy(&work_ptr[pte_ix], &pte, sizeof(uint64_t));
     
     pde.fields.present    = 1;
     pde.fields.read_write = 1;
     
     work_ptr = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits);
     memcpy(&work_ptr[pde_ix], &pde, sizeof(uint64_t));
     
     pdpte.fields.present    = 1;
     pdpte.fields.read_write = 1;
     
     work_ptr = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits);
     memcpy(&work_ptr[pdpte_ix], &pdpte, sizeof(uint64_t));
     

    write_cr3(page_manager.page_phys_base);
 
    return(0);
}