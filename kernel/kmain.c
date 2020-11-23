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
#include <intc.h>
#include <utils.h>
#include <isr.h>
#include <sched.h>

int isr_test(void)
{
    kprintf("%s\n",__FUNCTION__);
    while(1);
    return(0);
}

void kmain()
{
    /*register CPU API */
    cpu_api_register();

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
        return;

    /* install ISR handlers for the page manager */
    if(pagemgr_install_handler())
        return;

    platform_early_init();

    /* initialize the CPU driver and the BSP */
    cpu_init();

    /* Start APs */
    cpu_ap_start();

    devmgr_show_devices();

   #include <platform.h>

    device_t *dev = devmgr_dev_get_by_name("APIC_TIMER", 0);
    device_t *cpu = devmgr_dev_get_by_name(PLATFORM_CPU_NAME,0);

    cpu_t *c = devmgr_dev_data_get(cpu);
        
#if 1
    int *j = 0;

    sched_cpu_init(dev, c);
    while(1)
    {
      
    

       // test_interrupt();

       // kprintf("LOOPING %d\n", ++j);
       /* timer_loop_delay(dev, 1);*/

   
    }
#endif
    while(1)
        halt();
}