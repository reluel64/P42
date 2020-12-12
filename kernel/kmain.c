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

long int multiplyNumbers(int n) {
    if (n>=1)
        return n*multiplyNumbers(n-1);
    else
        return 1;
}


int entry_pt(void *p)
{
    kprintf("p %x\n",p);
    while(1)
    {
       //kprintf("FACT1 %d\n",multiplyNumbers(10));
    }
}
int entry_pt2(void *p)
{//
    kprintf("p %x\n",p);
    
    while(1)
    {
       // kprintf("FACT2 %d\n",multiplyNumbers(5));
    }
}

int entry_pt3(void *p)
{//
    kprintf("p %x\n",p);
    
    while(1)
    {
       // kprintf("FACT3 %d\n",multiplyNumbers(2));
    }
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
    cpu_ap_start(-1, PLATFORM_AP_START_TIMEOUT);

    sched_init();

   #include <platform.h>
    
    device_t *dev = devmgr_dev_get_by_name("APIC_TIMER", 0);
    device_t *cpu = devmgr_dev_get_by_name(PLATFORM_CPU_NAME,0);

    cpu_t *c = devmgr_dev_data_get(cpu);
       
#if 1
    sched_thread_t th1;
    sched_thread_t th2;
    sched_thread_t th3;

    memset(&th1, 0, sizeof(sched_thread_t));
    memset(&th2, 0, sizeof(sched_thread_t));
    memset(&th3, 0, sizeof(sched_thread_t));
    sched_cpu_init(dev, c);

    kprintf("Hello World\n");
    timer_loop_delay(dev, 1000);
    devmgr_show_devices();

    sched_init_thread(&th1, entry_pt, 0x1000, 0,0x11223344);
    sched_init_thread(&th2, entry_pt2, 0x1000, 0, 0xAABBCCDD);
    sched_init_thread(&th3, entry_pt3, 0x1000, 0, 0xAABBCCDD);
    

    sched_start_thread(&th2);
    sched_start_thread(&th1);
    sched_start_thread(&th3);

    while(1)
    {
      
    

       // test_interrupt();

       // kprintf("LOOPING %d\n", ++j);
       /* timer_loop_delay(dev, 1);*/

   
    }
#endif

  
}
