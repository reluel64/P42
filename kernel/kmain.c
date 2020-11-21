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
int smp_start_cpus(void);

extern int cpu_ap_setup(uint32_t cpu_id);
extern void __tsc_info(uint32_t *, uint32_t *, uint32_t *);

extern int pcpu_ap_start(uint32_t cpu_start_id);
void kmain()
{
   
    uint32_t den = 0;
    uint32_t num = 0;
    uint32_t crystal_hx = 0;
    uint32_t bus = 0;
    uint32_t base = 0;

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
   
    vga_init();

    /* initialize interrupt handler */
    if(isr_init())
        return;
kprintf("HELLO WORLKD\n");
    /* install ISR handlers for the page manager */
    if(pagemgr_install_handler())
        return;

    platform_early_init();

    /* initialize the CPU driver and the BSP */
    cpu_init();

    cpu_ap_start();



   list_head_t head;
   list_node_t *node = NULL;
   list_node_t *next = NULL;


   linked_list_init(&head);


   for(int  i = 0; i < 200; i++)
   {
       node = kcalloc(1, sizeof(list_node_t));
       linked_list_add_tail(&head, node);
   }

    node = linked_list_first(&head);

    while(node)
    {
        next = linked_list_next(node);

        linked_list_remove(&head, node);

        kfree(node);
        node = next;
    }



    devmgr_show_devices();

#if 0
    int j = 0;
    while(1)
    {
        

        kprintf("LOOPING %d\n", ++j);
        timer_loop_delay(NULL, 10);
    }
#endif






    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    eax = 0x6;

    __cpuid(&eax,&ebx,&ecx,&edx);


    kprintf("EAX %x EBX %x ECX %x EDX %x\n",eax & (1 << 2),ebx,ecx,edx);

   // cpu_int_check();


   // vmmgr_list_entries();
}