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
    pagemgr_init();

    /* Initialize Virtual Memory Manager */
    vmmgr_init();

    /* Initialize Physical Memory manager */
    physmm_init();

    vmmgr_list_entries();
}
