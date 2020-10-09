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
#include <pic.h>
int apic_timer_start(uint32_t timeout);

uint8_t *apic_base ;extern void physmm_dump_bitmaps(void);

extern uint64_t start_ap_begin;
extern uint64_t start_ap_end;
extern int physf_early_init(void);
extern int physf_early_alloc_pf(phys_size_t pf, uint8_t flags, alloc_cb cb, void *pv);

extern int physf_init(void);
extern int acpi_mem_mgr_on();

static int test_callback(phys_addr_t addr, phys_size_t count, void *pv)
{
    kprintf("Address 0x%x Count 0x%x\n",addr, count);
    return(0);
}

static int acpi_init(void)
{
    ACPI_STATUS status = AE_OK;
#if 0
    status = AcpiInitializeSubsystem();

    if(ACPI_FAILURE(status))
    {
        kprintf("Failed to initialize ACPI Subsystem\n");
        return(status);
    }
#endif
    status = AcpiReallocateRootTable();
    
    if(ACPI_FAILURE(status))
    {
        kprintf("Failed to reallocate the RootTable\n");
        return(status);
    }

    status = AcpiLoadTables();

    if(ACPI_FAILURE(status))
    {
        kprintf("Failed to reallocate the load tables\n");
        return(status);
    }

#if 1

    status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);

    if(ACPI_FAILURE(status))
    {
        kprintf("Failed to enable susbsystem\n");
        return(status);
    }

    status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    
    if(ACPI_FAILURE(status))
    {
        kprintf("Failed to initialize objects\n");
        return(status);
    }
#endif

    kprintf("InitDone\n");
    return(status);

}
int smp_start_cpus(void);
extern void _sgdt(gdt64_ptr_t *gdt);
extern int cpu_ap_setup(uint32_t cpu_id);
extern void __tsc_info(uint32_t *, uint32_t *, uint32_t *);

void kmain()
{
    /* Init polling console */
    init_serial();
    pagemgr_boot_temp_map_init();

    pfmgr_early_init();

    /* Initialize Virtual Memory Manager */
    if(vmmgr_init() != 0)
        return;

    /* Initialize Page Frame Manager*/
    if(pfmgr_init() != 0)
        return;
   
    vga_init();



    uint32_t den = 0;
    uint32_t num = 0;
    uint32_t crystal_hx = 0;
    uint32_t bus = 0;
    uint32_t base = 0;
    __tsc_info(&den, &num, &crystal_hx);
    __freq_info(&bus, &base);
    kprintf("DEN %d NUM %d HZ %d\n",den,num, crystal_hx);
    kprintf("bus %d base %d\n",bus,base);
       // while(1);
    vga_print("CPU_INIT\n",0x7,-1);


    platform_register();
    platform_init();

   // ioapic_probe();

    
    vga_print("CPU_INIT_DONE\n",0x7,-1);
    while(1);


    vga_print("smp_start_cpus\n",0x7,-1);
    smp_start_cpus();
    vga_print("smp_start_cpus_DONE\n",0x7,-1);
    extern virt_addr_t __stack_pointer();
    /*kprintf("CHECKING APIC 0x%x\n", apic_is_bsp());*/
    kprintf("HELLO TOP 0x%x\n",__stack_pointer());
    
   
   // cpu_init();
    
  //  smp_init();
    /*acpi_init();*/


    
}

