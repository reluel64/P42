#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
#include <liballoc.h>
#include <spinlock.h>

int apic_timer_start(uint32_t timeout);

uint8_t *apic_base ;extern void physmm_dump_bitmaps(void);
extern int vmmgr_change_attrib(virt_addr_t virt, virt_size_t len, uint32_t attr);
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
        return;
    }
    
    acpi_init();
    acpi_read_tables();
}