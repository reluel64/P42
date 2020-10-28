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

int smp_start_cpus(void);
extern void _sgdt(gdt64_ptr_t *gdt);
extern int cpu_ap_setup(uint32_t cpu_id);
extern void __tsc_info(uint32_t *, uint32_t *, uint32_t *);

void kmain()
{
   
    uint32_t den = 0;
    uint32_t num = 0;
    uint32_t crystal_hx = 0;
    uint32_t bus = 0;
    uint32_t base = 0;

    /* init polling console */
    init_serial();

    /* initialize temporary mapping */
    pagemgr_boot_temp_map_init();
    
    /* initialize early page frame manager */
    pfmgr_early_init();
    
    /* initialize device manager */
    devmgr_init();
    
    /* initialize Virtual Memory Manager */
    if(vmmgr_init() != 0)
        return;

    /* initialize Page Frame Manager*/
    if(pfmgr_init() != 0)
        return;
   
    vga_init();


    __tsc_info(&den, &num, &crystal_hx);
    __freq_info(&bus, &base);
    kprintf("DEN %d NUM %d HZ %d\n",den,num, crystal_hx);
    kprintf("bus %d base %d\n",bus,base);

    vga_print("CPU_INIT\n",0x7,-1);
    
    platform_register();
    platform_init();
    kprintf("%s %d\n",__FUNCTION__,__LINE__);

    vga_print("DONEEEEEEE\n",0x7,-1);
    vga_print("CPU_INIT_DONE\n",0x7,-1);


    vga_print("smp_start_cpus\n",0x7,-1);
    //smp_start_cpus();
    vga_print("smp_start_cpus_DONE\n",0x7,-1);
    extern virt_addr_t __stack_pointer();
    /*kprintf("CHECKING APIC 0x%x\n", apic_is_bsp());*/
    kprintf("HELLO TOP 0x%x\n",__stack_pointer());

    while(1);
    
  


}

