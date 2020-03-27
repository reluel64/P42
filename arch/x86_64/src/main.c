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
    {// kprintf("%s: BITS 0x%x VIRT 0x%x\n",__FUNCTION__,path->pt[path->pt_ix].bits, virt);
       kprintf("ERROR\n");
        return;
    }
    pagemgr_t *pm = pagemgr_get();
  extern int vmmgr_change_attrib(uint64_t virt, uint64_t len, uint32_t attr);
  extern int vmmgr_free(void *vaddr, uint64_t len);
    //apic_timer_start(10000000);
extern int vmmgr_unmap(void *vaddr, uint64_t len);
vmmgr_list_entries();
kprintf("Hello World\n");
#if 1
   for(uint32_t j = 0; j<UINT32_MAX;j++)
    {
    void *i = vmmgr_alloc(0, 0x1000,0);
    
    if(i == NULL)
        break;

    kprintf("i = 0x%x\n",i);
      //  vmmgr_free(i,0x1000);
    //vmmgr_list_entries();
    }
#endif
   
}
