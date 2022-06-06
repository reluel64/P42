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
       kprintf("DEVICE 0x%x 0x%x\n",vendor, device);
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
    kprintf("Platform init\n");

    platform_init();

    vga_print("Platform init done\n");
    
    for(int bus = 0; bus < 256; bus++)
    {
        for(int slot = 0; slot < 32; slot++)
        {
            pciCheckVendor(bus, slot);
        }
    }

    virt_addr_t addr = vm_alloc(NULL, 0xffffffff84000000, 0x3000, 0, VM_ATTR_WRITABLE);

    if(addr != VM_INVALID_ADDRESS)
    {
        vm_change_attr(NULL, addr, 0x1000, 0, VM_ATTR_WRITABLE, NULL);
        vm_change_attr(NULL, addr + 0x2000, 0x1000, 0, VM_ATTR_WRITABLE, NULL);

        kprintf("addr %x\n",addr);
    }

    vm_ctx_show(NULL);


    while(1)
    {
        sched_sleep(1000);
    }
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
