#include <cpu.h>
#include <isr.h>
#include <intc.h>
#include <pagemgr.h>
#include <apic.h>
#include <pic8259.h>
#include <devmgr.h>
#include <ioapic.h>
extern int pcpu_init(void);
extern int apic_register(void);
extern int pic8259_register(void);
extern int acpi_mem_mgr_on(void);
extern int ioapic_register(void);
extern int pit8254_register(void);

int platform_register(void)
{
    return(0);
}

int platform_init(void)
{
    dev_t *dev  = NULL;
    
    if(pagemgr_install_handler() != 0)
        return(-1);

    acpi_mem_mgr_on();

    /* Register and initalize interrupt controllers */
    pic8259_register();
    ioapic_register();

    /* register and initialize PIT */
    pit8254_register();

    
}
