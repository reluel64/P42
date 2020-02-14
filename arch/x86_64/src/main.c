#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
void kmain()
{
    kprintf("Entered %s\n",__FUNCTION__);

    vga_init();
    init_serial();
  //  setup_descriptors();
 //   load_descriptors();
    //vga_print("Hello World",0x7,-1);
    physmm_init();
    pagemgr_init();
}
