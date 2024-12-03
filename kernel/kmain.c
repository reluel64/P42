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
#include <sched.h>
#include <semaphore.h>
#include <mutex.h>
#include <context.h>
#include <thread.h>
#include <owner.h>
#include <i8254.h>
#include <io.h>

sem_t *kb_sem = NULL;

void *kmain_sys_init
(
    void *arg
)
{
    kprintf("Performing platform initialization...\n");
    platform_init();    
    kprintf("Platform init complete\n");
    pfmgr_show_free_memory();
    vm_ctx_show(NULL);
    while(1)
    {
        sched_sleep(1000);
        kprintf("Testttt\n");
    }

    return(NULL);
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
