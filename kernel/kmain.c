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
static void test_thread(void *pv)
{
   
    while(1)
    {
        
       
       //mtx_acquire(&mtx, WAIT_FOREVER);
        sched_sleep(1000);
      //  kprintf("%d SELLPING on CPU %d\n", pv, cpu_id_get());
        //mtx_release(&mtx);
        
    }
}


static void pci_test(void)
{
    ACPI_TABLE_MCFG *mcfg = NULL;
   AcpiGetTable(ACPI_SIG_MCFG, 0, (ACPI_TABLE_HEADER**)&mcfg);

   kprintf("MCFG %x LEN %d\n",mcfg, mcfg->Header.Length);
}

void kmain_sys_init(void *arg)
{
    int hr = 0;
    int min = 0;
    int sec = 0;
    mtx_init(&mtx, 0);
    /* Start APs */
    kprintf("starting APs\n");
    
    kprintf("Platform init\n");

    platform_init();
    virt_size_t alloc_sz = 1024 * 1024;
    virt_addr_t addr = 0;
vga_print("HELLO\n");
  kprintf("BEFORE LOOP - ");
 // cpu_int_lock();
         static sched_thread_t th[500];
    mtx_acquire(&mtx, WAIT_FOREVER);
    #if 0
         for(int i = 0; i < sizeof(th) / sizeof(sched_thread_t); i++)
         {
            thread_create_static(&th[i], test_thread,i, 0x1000, 100);
            thread_start(&th[i]);
         }
#endif
 mtx_release(&mtx);
         
    while(1)
    {
      //  mtx_acquire(&mtx, WAIT_FOREVER);
     
 
      //   vm_list_entries();
     // pgmgr_per_cpu_init();


    for(int i  = 0; i < 100; i++)
    {

#if 1
      addr = vm_alloc(NULL,0xffff800040001000 + alloc_sz , alloc_sz,0, VM_ATTR_WRITABLE);

       // kprintf("DONE %x\n", addr);

        vm_free(NULL,addr, alloc_sz);
        alloc_sz += 1024 * 1024;
     //   kprintf("AGAIN\n");
#endif
    }

        //vm_free(NULL, addr, alloc_sz );
     
        vm_ctx_show(NULL);
        kprintf("PHYS_MEMORY\n");
        pfmgr_show_free_memory();
       pci_test();
      
        vm_ctx_t test_ctx;

        memset(&test_ctx, 0, sizeof(vm_ctx_t));

        vm_user_ctx_init(&test_ctx);

        vm_alloc(&test_ctx, VM_BASE_AUTO, 0x1000, 0, VM_ATTR_WRITABLE);
        vm_ctx_show(&test_ctx);
        kprintf("KERNEL\n");

        vm_ctx_show(NULL);
        while(1)
        {
            
          kprintf("INT_CHECK %x\n",cpu_int_check());
           // mtx_acquire(&mtx, WAIT_FOREVER);
          //kprintf("CPU INT %d\n",cpu_int_check());
        //  mtx_release(&mtx);
        for(int i = 0; i < UINT32_MAX; i++);

        kprintf("HELLO\n");
        cpu_int_unlock();
        //  sched_sleep(100);
        }
      //  kprintf("XXXX %d\n",cpu_int_check());
      //  kprintf("TEST\n");
    //    kprintf("XXXX %d\n",cpu_int_check());
      
    //    sched_sleep(100000);
       // for(int i = 0; i < INT32_MAX / 2 - 1; i++);
      //  kprintf("ENDED\n");
    //    mtx_release(&mtx);

    while(1);
    }
    

   vga_print("Hello World\n");

    for(int bus = 0; bus < 256; bus++)
    {
        for(int slot = 0; slot < 32; slot++)
        {
            pciCheckVendor(bus, slot);
        }
    }

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
        return;
    
    /* initialize interrupt handler */
    if(isr_init())
        return;

    /* Initialize pgmgr */
    if(pgmgr_init())
        return;

    /* initialize Virtual Memory Manager */
    if(vm_init())
        return;

    /* initialize Page Frame Manager*/
    if(pfmgr_init())
        return;
        
    /* Initialize base of the scheduler */
    
    if(sched_init())
        return;
    
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
