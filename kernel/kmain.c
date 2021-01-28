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
#include <scheduler.h>
#include <semaphore.h>
#include <mutex.h>

static sched_thread_t init_th;

uint16_t pciConfigReadWord (uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint16_t tmp = 0;
 
    /* create configuration address as per Figure 1 */
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
 
    /* write out the address */
    __outd(0xCF8, address);
    /* read in the data */
    /* (offset & 2) * 8) = 0 will choose the first word of the 32 bits register */
    tmp = (uint16_t)((__ind(0xCFC) >> ((offset & 2) * 8)) & 0xffff);
    return (tmp);
}

uint16_t pciCheckVendor(uint8_t bus, uint8_t slot) {
    uint16_t vendor, device;
    /* try and read the first configuration register. Since there are no */
    /* vendors that == 0xFFFF, it must be a non-existent device. */
    if ((vendor = pciConfigReadWord(bus,slot,0,0)) != 0xFFFF) {
       device = pciConfigReadWord(bus,slot,0,2);
       kprintf("HELLO WORLD %x %x\n",vendor, device);
    } return (vendor);
}

mutex_t *mtx = NULL;
static sched_thread_t th[2];

 void kmain_th_1(void)
{
    while(1)
    {
       mtx_acquire(mtx, WAIT_FOREVER);
{
       kprintf("SLEEPING on CPU %d\n",cpu_id_get());
       sched_sleep(2000);
       // for(uint32_t i = 0; i< UINT32_MAX/4;i++);
        
        kprintf("DONE\n");

        mtx_release(mtx);
    }
    }
}

static void kmain_th_2(void)
{
    while(1)
    {
        if(mtx_acquire(mtx, 100)==0)
        {
            kprintf("Hello World on cPU %d\n", cpu_id_get());
            
        }   
        else
        {
            kprintf("FAIL\n");
           
        }
        mtx_release(mtx);
     
    }
}


static void kmain_sys_init(void)
{
    int hr = 0;
    int min = 0;
    int sec = 0;

    /* Start APs */
    kprintf("starting APs\n");
    cpu_ap_start(-1, PLATFORM_AP_START_TIMEOUT);
    kprintf("Platform init\n");
    platform_init();

   vga_print("Hello World\n");

    for(int bus = 0; bus < 256; bus++)
    {
        for(int slot = 0; slot < 32; slot++)
        {
            pciCheckVendor(bus, slot);
        }
    }
    mtx = mtx_create();
    sched_init_thread(&th[0], kmain_th_1, 0x1000, 0, 0);

    /* Enqueue the thread */
    sched_start_thread(&th[0]);


    sched_init_thread(&th[1], kmain_th_2, 0x1000, 0, 0);

    /* Enqueue the thread */
    sched_start_thread(&th[1]);

#if 1
    while(1)
    {
        

        sched_sleep(1000);
        sec++;

        if(sec == 60)
        {
            min++;
            sec = 0;
        }

        if(min == 60)
        {
            min = 0;
            sec = 0;
            hr++;
        }
        vga_print("HELLO\n");
       // kprintf("HELLO WORLD %d:%d:%d\n",hr,min,sec);

    }
#endif


}


/* Kernel entry point */

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

    /* initialize interrupt handler */
    if(isr_init())
        return;

    /* install ISR handlers for the page manager */
    if(pagemgr_install_handler())
        return;

    /* Initialize basic platform functionality */
    platform_early_init();
    
    /* Initialize base of the scheduler */
    sched_init();

    /* Prepare the initialization thread */
    sched_init_thread(&init_th, kmain_sys_init, 0x1000, 0, 0);

    /* Enqueue the thread */
    sched_start_thread(&init_th);

    /* initialize the CPU driver and the BSP */
    if(cpu_init())
    {
        kprintf("FAILED TO PROPERLY SET UP BSP\n");
        while(1)
        {
            cpu_halt();
        }
    }
}
