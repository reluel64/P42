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

struct sem *kb_sem = NULL;
struct mutex mtx;
struct sem sem;
void *test_thread(void *arg)
{
    while(1)
    {
        sem_acquire(&sem, WAIT_FOREVER);
        kprintf("TASK %d CPU %d\n",sched_thread_self()->context_switches, cpu_id_get());
        
        
        
    }
}

extern int vm_merge(void);
void test_func(void)
{
   static  struct sched_thread th[1];


  //  memset(all, 0,0x400000000 );
    for(int i = 0; i < sizeof(th) / sizeof(th[0]); i++)
    {

   

       kthread_create_static(&th[i], NULL, test_thread, NULL, 0x1000, 100, NULL);
        
       //
        thread_start(&th[i]);       
    }
}

extern int vm_extent_optimize(void);


int32_t cpu_call(void *pv)
{
       kprintf("ISR CPU ID %d\n",cpu_id_get());
       return(0);
}

int32_t cpu_enqueue_call
(
    uint32_t cpu_id,
    int32_t (*ipi_handler)(void *pv),
    void *pv
);

void *kmain_sys_init
(
    void *arg
)
{
    kprintf("Performing platform initialization...\n");
    platform_init();    
    
    mtx_init(&mtx, MUTEX_RECUSRIVE | MUTEX_FIFO);
    sem_init(&sem, 0, 1);
   test_func();
    kprintf("Platform init complete\n");

    int fd = open("vga",0,0);
    char b[] = "HELLORORORORO\nRORORORORORORO";
    if(fd != -1)
    {
       write(fd, b, strlen(b));
    }
    
     
  //  pci_enumerate();    
    int i = 0;
    while(1)
    {
        
       // mtx_acquire(&mtx, WAIT_FOREVER);
        sched_sleep(1000);
        sem_release(&sem);
        //kprintf("Testttt %d\n", i++);
          //  pfmgr_show_free_memory();
        
        //vm_merge();
       // mtx_release(&mtx);
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
