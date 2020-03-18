#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>

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
        
    if(init_gdt() != 0)
        return;

    if(init_isr()!= 0)
        return;
    kprintf("Hello\n");

   // interrupt_call();
    
    /* Trigger a PF */
    char *p = 0x10000;
    kprintf("Hello World %s\n",p);

}
