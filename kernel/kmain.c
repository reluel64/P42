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

static sched_thread_t th[4];

static mutex_t mtx;
static void test_thread(void *pv)
{
 
 
    while(1)
    {
       
       mtx_acquire(&mtx, -1);


       kprintf("THREAD ID %x - entry point %s\n",sched_thread_self(), __FUNCTION__);
        sched_sleep(100);
      
     
        mtx_release(&mtx);

        
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

static void *test_waker(void *th)
{
    while(1)
    {
        mtx_acquire(&mtx, -1);
        kprintf("THREAD ID %x - entry point %s\n",sched_thread_self(), __FUNCTION__);
        sched_sleep(1000);
        mtx_release(&mtx);
    }
}

extern int devmgr_dev_remove
(
    device_t *dev, 
    uint8_t remove_children
);

extern int debug_pgmgr = 0;

void *kmain_sys_init
(
    void *arg
)
{

    device_t *root_parent = NULL;
    device_t *parent = NULL;
    device_t *child = NULL;


    devmgr_dev_create(&parent);

    devmgr_dev_name_set(parent,"1");
    devmgr_dev_type_set(parent,"1");

     devmgr_dev_add(parent,NULL);

    root_parent = parent;

    devmgr_dev_create(&child);
    devmgr_dev_name_set(child,"1-1");
    devmgr_dev_type_set(child,"1-1");
    devmgr_dev_add(child,parent);

    child = NULL;
    devmgr_dev_create(&child);
    devmgr_dev_name_set(child,"1-2");
    devmgr_dev_type_set(child,"1-2");
    devmgr_dev_add(child,parent);


    child = NULL;
    devmgr_dev_create(&child);
    devmgr_dev_name_set(child,"1-3");
    devmgr_dev_type_set(child,"1-3");
    devmgr_dev_add(child,parent);

    parent = devmgr_dev_get_by_name("1-2", 0);



    child = NULL;
    devmgr_dev_create(&child);
    devmgr_dev_name_set(child,"1-2-1");
    devmgr_dev_type_set(child,"1-2-1");
    devmgr_dev_add(child,parent);

    parent=child;

    child = NULL;
    devmgr_dev_create(&child);
    devmgr_dev_name_set(child,"1-2-1-1");
    devmgr_dev_type_set(child,"1-2-1-1");
    devmgr_dev_add(child,parent);


    child = NULL;
    devmgr_dev_create(&child);
    devmgr_dev_name_set(child,"1-2-1-2");
    devmgr_dev_type_set(child,"1-2-1-2");
    devmgr_dev_add(child,parent);

   

    kprintf("Platform init\n");
  
    platform_init();

    


    mtx_init(&mtx,MUTEX_FIFO);

    char bbb[] = "A\nl\ni\nn\na\n" ;
    
    int fd = -1;

    fd = open("vga", 0, 0);

    write(fd,bbb,strlen(bbb));

    static sched_thread_t waker;

    kthread_create_static(&waker, test_waker, NULL, 0x1000, 100, NULL);
    thread_start(&waker);
    kprintf("HELLO\n");
   // devmgr_show_devices();
  //       cpu_t *c = cpu_current_get();
        
        devmgr_dev_remove(root_parent, 1);
   //devmgr_show_devices();
#if 0
//kprintf("++++++++++++++++++++++++++++++++++++++++++++\n");
    for(int i = 0; i < sizeof(th) / sizeof(sched_thread_t);i++)
    {
        kthread_create_static(&th[i],(th_entry_point_t)test_thread, (void*)(uint64_t)i, 0x1000, 200, NULL);

        thread_start(&th[i]);
    }
    #endif  
   

    virt_size_t alloc = 15360ull * 1024ull * 1024ull;
    size_t loop = 0;
    #if 1
    while(loop < 10)
    {
        kprintf("LOOP START %d\n", loop);
  //   kprintf("ALLOC\n");
  debug_pgmgr = 1;
        virt_addr_t v = vm_alloc(NULL,VM_BASE_AUTO, alloc, 0, VM_ATTR_WRITABLE);
    //    kprintf("ALLOC DONE\n");
     //   kprintf("ALLOC DONE\n");
       // kprintf("ALLOC DONE\n");
  //  kprintf("V = %x\n",v);
    /* mark the first guard page as read-only */

    if(v != VM_INVALID_ADDRESS)
    {
     //  kprintf("FREEING\n");
        vm_free(NULL, v, alloc);
    //    kprintf("FREE DONE\n");
  #if 0
    vm_change_attr(NULL, 
                  v,
                  PAGE_SIZE, 0, 
                  VM_ATTR_WRITABLE, 
                  NULL);

    /* mark the last guard page as read only */
    vm_change_attr(NULL, 
                  v + alloc - PAGE_SIZE,
                  PAGE_SIZE, 0, 
                  VM_ATTR_WRITABLE, 
                  NULL);
                  #endif
    }
    else
    {
        break;
    }
//kprintf("ATTRIB CHANGE DONE\n");
        // vm_free(NULL, v, alloc);
      //   kprintf("LOOP END %d\n", loop);
         loop++;
         // sched_sleep(1000);

    }
    vm_ctx_show(NULL);
    pfmgr_show_free_memory();
    kprintf("ENDED\n");





    #endif
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
