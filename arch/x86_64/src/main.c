#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
#include <vmmgr.h>
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

    vmmgr_list_entries();

    physmm_test();
}
