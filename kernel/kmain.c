#include <stdint.h>

#include <utils.h>
#include <pfmgr.h>
#include <pgmgr.h>
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
#include <owner.h>
#include <i8254.h>
#include <io.h>

sem_t *kb_sem = NULL;
static void test_thread(void *pv)
{
 
 
    while(1)
    {
       sched_sleep(1000);        
    }
}

static void *test_waker(void *th)
{
    const virt_size_t alloc_size = 1024ull*1024ull*16384ull;
    virt_addr_t v[32];
    

    while(1)
    {
      #if 0  
        for(int i = 0; i < 16; i++)
        {
          //  kprintf("Allocating on slot %d\n", i);
            v[i] = vm_alloc(NULL, VM_BASE_AUTO, alloc_size, 0, VM_ATTR_WRITABLE);
          //  kprintf("Allocated on slot %d - 0x%x\n", i, v[i]);
        }
#endif

       #if 0 
        if(sem_acquire(kb_sem, 1000) == 0)
        {
            kprintf("DATA 0x60 %x DATA 0x64 %x\n",__inb(0x60), __inb(0x64));
        }
        #endif
        kprintf("Test %x\n", sched_thread_self());
        sched_sleep(1000);
        

        #if 0
        for(int i = 0; i < 16; i++)
        {
            //kprintf("PTR = %x\n",v[i]);
            vm_free(NULL, v[i], alloc_size);
        }
#endif
      //  kprintf("Sem woken up\n");
    }
}

extern int devmgr_dev_remove
(
    device_t *dev, 
    uint8_t remove_children
);

void *kmain_sys_init
(
    void *arg
)
{
    static sched_thread_t waker ={0};
    static sched_thread_t waker2 = {0};
    kprintf("Platform init\n");
  
    platform_init();

    kb_sem = sem_create(0, 1);
    kthread_create_static(&waker, test_waker, NULL, 0x1000, 100, NULL);
  //  kthread_create_static(&waker2, test_thread, NULL, 0x1000, 100, NULL);
    thread_start(&waker);
   // thread_start(&waker2);
   while(1);
    
}

/* Kernel entry point */

void kmain()
{
    /* prepare very basic platform functionality */
    platform_pre_init();

    /* initialize early page frame manager */
    pfmgr_early_init();
 
    /* initialize device manager */
    if(devmgr_init())
    {
        return;
    }

    if(io_init())
    {
        return;
    }

    /* initialize interrupt handler */
    if(isr_init())
    {
        return;
    }

    /* Initialize pgmgr */
    if(pgmgr_init())
    {
        return;
    }

    /* initialize Virtual Memory Manager */
    if(vm_init())
    {
        return;
    }
    /* initialize Page Frame Manager*/
    if(pfmgr_init())
    {
        return;
    }

    /* Initialize base of the scheduler */
    if(sched_init())
    {
        return;
    }
    
    /* initialize system timer structure */
    if(timer_system_init())
    {
        return;
    }

    /* Initialize basic platform functionality */
    platform_early_init();

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
