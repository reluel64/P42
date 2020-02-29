#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
#include <vmmgr.h>
void kmain()
{
    kprintf("Entered %s\n",__FUNCTION__);

    vga_init();
    init_serial();
    
    physmm_init();
    
    pagemgr_init();
    vmmgr_init();
    kprintf("HAs NX %d\n",has_nx());
    kprintf("HAS PML5 %d\n",has_pml5());
    kprintf("MAX_PHYS %d\n",max_physical_address());
    kprintf("MAX_LINEAR %d\n",max_linear_address());
}
