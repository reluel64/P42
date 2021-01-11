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

semaphore_t *sem  = NULL;
semaphore_t *sem2 = NULL;
mutex_t *mtx = NULL;

sched_thread_t init_th;
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

int counter = 0;

int entry_pt(void *p)
{
    int x = 0;
    while(1)
    {
    mtx_acquire(mtx);

    kprintf("p %d\n",counter);
    mtx_release(mtx);
    }
}

int entry_pt2(void *p)
{
  
    while(1)
    {
        

        mtx_acquire(mtx);
        sched_sleep(1000);
        
         kprintf("Hello World\n");
        //sched_sleep(1);
       //kprintf("Hello\n");
        counter++;
        mtx_release(mtx);
      
    }
}

int entry_pt3(void *p)
{//
    
   
      //sem_acquire(sem);

     // kprintf("GOODBYE World\n");
    //if(th != p)
        //kprintf("p %x %x\n",th,p);
    
}

#include <liballoc.h>
#include <utils.h>

static void *alloc_align(size_t sz, size_t align)
{
    size_t total = 0;
    uint8_t *buf = NULL;
    total = sz + align;

    buf = kmalloc(total);

    buf = ALIGN_UP((uint64_t)buf, align);

    return(buf);
}

static void kmain_sys_init(void)
{
    /* Start APs */
    cpu_ap_start(-1, PLATFORM_AP_START_TIMEOUT);
    platform_init();
    vga_print("Hello World\n");


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
