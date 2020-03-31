#include <stdint.h>
#include <vga.h>
#include <serial.h>
#include <utils.h>
#include <physmm.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
#include <liballoc.h>
#include <spinlock.h>

int apic_timer_start(uint32_t timeout);

uint8_t *apic_base ;extern void physmm_dump_bitmaps(void);
extern int vmmgr_change_attrib(virt_addr_t virt, virt_size_t len, uint32_t attr);
void kmain()
{

    kprintf("P42 Kernel\n");
    /* Init polling console */
    init_serial();

    /* Init early physical memory manager */
    physmm_early_init();

    /* Initialize page manager*/
    if(pagemgr_init() != 0)
        return;

    /* Initialize Virtual Memory Manager */
    if(vmmgr_init() != 0)
        return;

    /* Initialize Physical Memory manager */
    if(physmm_init() != 0)
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
    {
       kprintf("ERROR\n");
        return;
    }

#if 0
pagemgr_t *pg = pagemgr_get();


    kprintf("------------------DUMP BEFORE------------------\n");
    physmm_dump_bitmaps();
    void *pp = vmmgr_alloc(0,4096ull*1024ull*1024ull,0);

    if(pp == NULL)
    {
        kprintf("FAILED\n");
        while(1);
    }

    vmmgr_free(pp,4096ull*1024ull*1024ull);

kprintf("------------------DUMP AFTER------------------\n");
    physmm_dump_bitmaps();

uint64_t i = 1024ull*1024ull;
    for( ;i<=4096ull*1024ull*1024ull ; i+=1024ull*1024ull )
    {
        void *x = NULL;
        uint64_t v = kmalloc(i);
        if(v== NULL)
        {   
        kprintf("V = 0x%x\n",v);
           break;
        }
        else
        {
         //  kprintf("Alloc OK 0x%x 0x%x\n", v, i);
        }
       // vmmgr_list_entries();
        // vmmgr_change_attrib(ALIGN_UP(v, PAGE_SIZE), i - PAGE_SIZE, ~VMM_ATTR_WRITABLE);
        // memset(v, 0xff,i);
        kfree(v);
       
        x = v;
    }
    kprintf("------------------DUMP DEAD------------------\n");
    kprintf("DEAD 0x%x\n",i);
    #endif
#if 1
    for(uint64_t i = 0; i < 512; i++)
    {
        void *v = vmmgr_alloc(0, PAGE_SIZE * i,0);

        vmmgr_reserve(v, PAGE_SIZE * i, 64);
    }
    #endif
    vmmgr_list_entries();
   // physmm_dump_bitmaps();
    while(1);
}