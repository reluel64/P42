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
int apic_timer_start(uint32_t timeout);

uint8_t *apic_base ;extern void physmm_dump_bitmaps(void);
extern int vmmgr_change_attrib(virt_addr_t virt, virt_size_t len, uint32_t attr);
extern uint64_t start_ap_begin;
extern uint64_t start_ap_end;
extern int physf_early_init(void);
extern int physf_early_alloc_pf(phys_size_t pf, uint8_t flags, alloc_cb cb, void *pv);

extern int physf_init(void);
static int test_callback(phys_addr_t addr, phys_size_t count, void *pv)
{
    kprintf("Address 0x%x Count 0x%x\n",addr, count);
    return(0);
}

void kmain()
{
    /* Init polling console */
    init_serial();


    pfmgr_early_init();

    /* Initialize page manager*/
    if(pagemgr_init() != 0)
        return;

    /* Initialize Virtual Memory Manager */
    if(vmmgr_init() != 0)
        return;

    /* Initialize Page Frame Manager*/
    if(pfmgr_init() != 0)
        return;
        
    if(gdt_init() != 0)
        return;

    if(isr_init()!= 0)
        return;

    if(pagemgr_install_handler() != 0)
        return;


    vga_init();
    disable_pic();

    if(lapic_init())
        return;

pagemgr_t *pg = pagemgr_get();
kprintf("ALLOC\n");
void *p = kmalloc(0x500000000);
if(p != NULL)
{
kprintf("ALLOC_DONE\n");
kfree(p);
}
kprintf("FreeDone\n");

uint64_t iter= 0;
#if 0
    for(uint64_t i = 1024*1024; i< 0x500000000;i+=1024*1024)
    {
        void *p = kmalloc(i);
        iter ++;
    
        if(p == NULL)
            break;
        kprintf("ALLOC_OK ITER  0x%x -> 0x%x\n",i, p);    
        //pg->dealloc(0xffff800001000000, i);
        kfree(p);
    }

     #endif   


AcpiOsInitialize();

kprintf("DONE\n");
//    extern int wake_cpu();

  //  wake_cpu();
}
