#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
int apic_timer_start(uint32_t timeout);

uint8_t *apic_base ;

void kmain()
{
    kprintf("P42 Kernel\n");
    /* Init polling console */
    init_serial();
    
    /* Init early physical memory manager */
    physmm_early_init();

    /* Initialize page manager*/
    if(pagemgr_init() != 0)
        return;

    /* Initialize Virtual Memory Manager */
    if(vmmgr_init() != 0)
        return;
    /* Initialize Physical Memory manager */
    if(physmm_init() != 0)
        return;
        
    if(gdt_init() != 0)
        return;

    if(isr_init()!= 0)
        return;

    if(pagemgr_install_handler() != 0)
        return;
     
    vga_init();
    disable_pic();

    if(lapic_init())
    {
       kprintf("ERROR\n");
        return;
    }
    pagemgr_t *pm = pagemgr_get();
  extern int vmmgr_change_attrib(uint64_t virt, uint64_t len, uint32_t attr);
    //apic_timer_start(10000000);
    char *i = vmmgr_alloc(8589934592, PAGE_WRITABLE);


extern int pagemgr_free(uint64_t vaddr, uint64_t len);

pagemgr_free(i, 8589934592);
i = vmmgr_alloc(8589934592, PAGE_WRITABLE);
 pagemgr_free(i, 8589934592);
//pagemgr_t *pm = pagemgr_get();
//pm->alloc(0x5000,PAGE_SIZE,PAGE_WRITABLE);

}
