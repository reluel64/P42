#include <stdint.h>

#include <utils.h>
#include <pfmgr.h>
#include <pagemgr.h>
#include <vm.h>
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
#include <context.h>
#include <thread.h>

static sched_thread_t th1;
static sched_thread_t th2;
static sched_thread_t init_th;
static sched_thread_t init_th2;
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
mutex_t mtx;
static void kmain_sys_init(void)
{
    int hr = 0;
    int min = 0;
    int sec = 0;

    /* Start APs */
    kprintf("starting APs\n");
    
    kprintf("Platform init\n");
    platform_init();

    virt_addr_t addr = 0;
   addr = vm_alloc(NULL,VM_BASE_AUTO, 1024ull*1024ull *1024ull * 8ull, 0, VM_ATTR_WRITABLE);
    
    kprintf("ADDR %x\n",addr);
    
    memset(addr, 0, 1024*1024 *2);
   // vm_unmap(NULL, addr, 1024*2048);
    while(1)
    {
      //  mtx_acquire(&mtx, WAIT_FOREVER);
             
        kprintf("XXXX %d\n",cpu_int_check());
        kprintf("TEST\n");
        kprintf("XXXX %d\n",cpu_int_check());
        sched_sleep(1000);
       // for(int i = 0; i < INT32_MAX / 2 - 1; i++);
      //  kprintf("ENDED\n");
    //    mtx_release(&mtx);
    }
    

   vga_print("Hello World\n");

    for(int bus = 0; bus < 256; bus++)
    {
        for(int slot = 0; slot < 32; slot++)
        {
            pciCheckVendor(bus, slot);
        }
    }
}




int func2(int i)
{
    kprintf("XXXX %d\n",cpu_int_check());
  //  schedule();
    return(0);
}

static void kmain_sys_init2(void *arg)
{
    int hr = 0;
    int min = 0;
    int sec = 0;

    /* Start APs */
    kprintf("starting APs 2\n");
    
    kprintf("Platform init\n");

    while(1)
    {

        mtx_acquire(&mtx, WAIT_FOREVER);;
        kprintf("TEST2 %x\n", arg);
      //  for(int i = 0; i < INT32_MAX/2 - 1; i++);
        func2(90);

        mtx_release(&mtx);
    }
    platform_init();

   vga_print("Hello World\n");

    for(int bus = 0; bus < 256; bus++)
    {
        for(int slot = 0; slot < 32; slot++)
        {
            pciCheckVendor(bus, slot);
        }
    }

}
/* Kernel entry point */

void kmain()
{

    platform_pre_init();

    /* initialize early page frame manager */
    pfmgr_early_init();
 
    /* initialize device manager */
    if(devmgr_init())
        return;
    
    /* initialize Virtual Memory Manager */
    if(vm_init())
        return;

    /* initialize Page Frame Manager*/
    if(pfmgr_init())
        return;

    /* initialize interrupt handler */
    if(isr_init())
        return;

    /* Initialize basic platform functionality */
    platform_early_init();
 
    /* Initialize base of the scheduler */
    sched_init();

    mtx_init(&mtx, 0);

    /* Prepare the initialization thread */
    thread_create_static(&init_th, kmain_sys_init,NULL, 0x1000, 200);
    thread_start(&init_th);
//    thread_create_static(&init_th2, kmain_sys_init2,NULL, 0x1000, 0);
//    thread_start(&init_th2);

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
