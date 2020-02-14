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
extern uint64_t _code;
extern uint64_t read_cr3();
extern uint64_t write_cr3(uint64_t phys_addr);
extern void     __invlpg(uint64_t address);

static pagemgr_t page_manager;

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
    uint64_t     *temp     = 0;
    uint64_t     phys_addr = 0;
    uint64_t     kernel_vaddr_start = &_code;
    uint16_t     pml4e_index = 0;
    uint16_t     pdpte_index = 0;
    uint16_t     pde_index   = 0;
    uint16_t     pte_index   = 0;
    uint16_t     page_index  = 0;

    memset(&page_manager, 0, sizeof(pagemgr_t));

    page_manager.page_phys_base = physmm_alloc_pf();
    
    /* clear the pml4 */
    
    pml4e.bits = physmm_alloc_pf();
    
    temp = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
    
    memset(temp, 0, PAGE_SIZE);

    temp = (uint64_t*)pagemgr_boot_temp_map(pml4e.bits);

    memset(temp, 0, PAGE_SIZE);

    pml4e_index = VIRT_TO_PML4_INDEX(kernel_vaddr_start);
    pdpte_index = VIRT_TO_PDPTE_INDEX(kernel_vaddr_start);
    pde_index   = VIRT_TO_PDE_INDEX(kernel_vaddr_start);
    pte_index   = VIRT_TO_PTE_INDEX(kernel_vaddr_start);


    kprintf("BS PML4: %d PDPTE %d PDE %d PTE %d\n",pml4e_index,pdpte_index,pde_index,pte_index);
    kprintf("ADDR 0x%x\n",kernel_vaddr_start);
    /* begin mapping of the kernel binary */

    #if 0


    for(uint64_t pdpt_ix = 510; pdpt_ix < 512;pdpt_ix++)
    {
        pdpte.bits = physmm_alloc_pf();

        /* Prepare PDPTE */
        temp =  (uint64_t*)pagemgr_boot_temp_map(pdpte.bits);
        memset(temp, 0, PAGE_SIZE);

        for(uint64_t pde_ix = 0; pde_ix < 512; pde_ix++)
        {
            pde.bits = physmm_alloc_pf();

            /* Prepare PDE */
            temp =  (uint64_t*)pagemgr_boot_temp_map(pde.bits);
            memset(temp, 0, PAGE_SIZE);

            for(uint64_t pt_ix = 0; pt_ix < 512; pt_ix++)
            {
                pte = phys_addr + 0x3;
                temp =  (uint64_t*)pagemgr_boot_temp_map(pde.bits);
                memcpy(&temp[pt_ix], &pte, sizeof(uint64_t));
                phys_addr+=PAGE_SIZE;
            }

            /* SAVE PDE */
            pde+=0x3;
            temp  = (uint64_t*)pagemgr_boot_temp_map(pdpte.bits);
            memcpy(&temp[pde_ix], &pde, sizeof(uint64_t));
            
        }


        /* Save PDPTE entry */
        pdpte+=0x3;
        temp  = (uint64_t*)pagemgr_boot_temp_map(pml4e);
        memcpy(&temp[pdpt_ix], &pdpte, sizeof(uint64_t));
    }
    
    temp = (uint64_t*)pagemgr_boot_temp_map(page_manager.page_phys_base);
    pml4e+=0x3;
    memcpy(&temp[511], &pml4e, sizeof(uint64_t));



    write_cr3(page_manager.page_phys_base);
   kprintf("ADDRESS 0x%x\n",page_manager.page_phys_base);
   // kprintf("Code Start 0x%x\n",phys_addr);
    

    kprintf("Hello world\n");
    while(1);
#endif
    return(0);

}