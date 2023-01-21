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
static sched_thread_t th[1];

static void test_thread(void *pv)
{
 

    while(1)
    {
        
       //mtx_acquire(&mtx, WAIT_FOREVER);
        sched_sleep(1000);
        kprintf("%d SLEEPING on CPU %d INT %d\n", pv, cpu_id_get(), cpu_int_check());
       
        //mtx_release(&mtx);
        
    }
}

static int pfmgr_alloc_pf_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    kprintf("ADDRESS 0x%x\n", cb_dat->phys_base);
    cb_dat->used_bytes+=0x1000;
    return(1);
}

void *kmain_sys_init
(
    void *arg
)
{
    kprintf("Platform init\n");

    platform_init();

    vga_print("Platform init done\n");
    
#if 1
//kprintf("++++++++++++++++++++++++++++++++++++++++++++\n");
    for(int i = 0; i < sizeof(th) / sizeof(sched_thread_t);i++)
    {
        kthread_create_static(&th[i],test_thread, i, 0x1000, 100, NULL);

        thread_start(&th[i]);
    }
    #endif

    vm_ctx_show(NULL);
    virt_size_t alloc = 21474836480ull;

    while(0)
    {
          kprintf("PRE ALLOC\n");
          virt_addr_t v = vm_alloc(NULL,VM_BASE_AUTO, alloc, 0, 0);
          kprintf("PRE FREE\n");

          vm_free(NULL, v, alloc);
          kprintf("POST FREE\n");
         // sched_sleep(1000);

    }

    kprintf("ENDED\n");
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
