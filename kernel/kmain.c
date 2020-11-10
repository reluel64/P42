#include <stdint.h>
#include <serial.h>
#include <utils.h>
#include <pfmgr.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
#include <liballoc.h>
#include <spinlock.h>
#include <acpi.h>
#include <devmgr.h>
#include <port.h>
#include <timer.h>
#include <cpu.h>
int smp_start_cpus(void);

extern int cpu_ap_setup(uint32_t cpu_id);
extern void __tsc_info(uint32_t *, uint32_t *, uint32_t *);

void kmain()
{
   
    uint32_t den = 0;
    uint32_t num = 0;
    uint32_t crystal_hx = 0;
    uint32_t bus = 0;
    uint32_t base = 0;

    /* init polling console */
    init_serial();

    /* initialize temporary mapping */
    pagemgr_boot_temp_map_init();
    
    /* initialize early page frame manager */
    pfmgr_early_init();
    
    /* initialize device manager */
    if(devmgr_init())
        return;
    
    /* initialize Virtual Memory Manager */
    if(vmmgr_init())
        return;

    /* initialize Page Frame Manager*/
    if(pfmgr_init())
        return;
   
    vga_init();

    /* initialize interrupt handler */
    if(isr_init())
        return(-1);

    /* install ISR handlers for the page manager */
    if(pagemgr_install_handler())
        return(-1);

    platform_init();

int  i = 0;
    while(1)
    {
        timer_loop_delay(NULL, 1000);
        kprintf("Hello %d\n", i++);
    }
}

