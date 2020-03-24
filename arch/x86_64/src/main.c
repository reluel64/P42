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
     
   // vga_init();
    disable_pic();
#if 0
    uint64_t apic_phys = read_apic() & ~(uint64_t)0xFFF;
    kprintf("APIC_VER 0x%x\n",apic_phys);
    
    apic_base = vmmgr_map(apic_phys,0,1,VMM_ATTR_WRITABLE | VMM_ATTR_WRITE_THROUGH | VMM_ATTR_NO_CACHE);
    kprintf("APIC_BASE 0x%x\n",apic_base);
  #if 0 
    uint64_t offset = 0xF0;
    uint32_t *svr   = (uint64_t)apic_base + offset;
    *svr = 0x1FF;

    offset = 0x3E0;
    svr = (uint64_t)apic_base + offset;
    *svr = 0x3;

    offset = 0x320;
    svr = (uint64_t)apic_base + offset;
    *svr = (32) | 1<< 17 ;

    offset = 0x380;
    svr = (uint64_t)apic_base + offset;
    svr[0] = 1000;

     offset = 0x280;
    svr = (uint64_t)apic_base + offset;
    #endif

     apic_init();
#endif
#if 0
    if(lapic_init())
    {
       kprintf("ERROR\n");
        return;
    }
    #endif
  

  char *pp = 0x1000;
  *pp = 0x1000;
//apic_timer_start(10000000);

//pagemgr_t *pm = pagemgr_get();
//pm->alloc(0x5000,PAGE_SIZE,PAGE_WRITABLE);


#if 0
unsigned long *ptr = (uint64_t*)0x0;

*ptr = 0xFFFFFFFF;
kprintf("0x%x\n", *ptr);
pp = 0x600000;
*pp=0x8;



kprintf("0x%x\n", *ptr);
#endif


}
