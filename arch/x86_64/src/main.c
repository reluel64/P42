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
 //physmm_test();
    void *p;
    {
       do
       {
       p = vmmgr_alloc(1024ull*1024ull*1ull,0);
//vmmgr_list_entries();
        kprintf("ADDR 0x%x\n",p);
       }while(p != NULL);
    }

    kprintf("DONE\n");
    
}
