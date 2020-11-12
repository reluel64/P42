#include <cpu.h>
#include <isr.h>
#include <intc.h>
#include <vmmgr.h>
#include <apic.h>
#include <pic8259.h>
#include <devmgr.h>
#include <ioapic.h>
#include <platform.h>

extern int pcpu_init(void);
extern int apic_register(void);
extern int pic8259_register(void);
extern int acpi_mem_mgr_on(void);
extern int ioapic_register(void);
extern int pit8254_register(void);

int platform_register(void)
{
    apic_register();
    return(0);
}

int platform_early_init(void)
{
    /* Tell ACPI code that now the memory manager is up and running */
    acpi_mem_mgr_on();

    /* Register and initalize interrupt controllers */

    /* 
     * It's important that PIC8259 is initialized before
     * IOAPIC as the IOAPIC driver will disable pic8259 
     * during the initialization process
     */

    pic8259_register();
    ioapic_register();

    /* register and initialize PIT */
    
    pit8254_register();

    apic_register();

}
