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
    
    if(gdt_init() != 0)
        return;

    if(isr_init()!= 0)
        return;

    if(pagemgr_install_handler() != 0)
        return;

    acpi_mem_mgr_on();

    vga_init();
    disable_pic();

    if(lapic_init())
        return;

    smp_init();
    /*acpi_init();*/


    
}

