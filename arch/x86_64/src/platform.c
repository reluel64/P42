#include <cpu.h>
#include <isr.h>
#include <intc.h>
#include <pagemgr.h>

extern int pcpu_init(void);
extern int apic_register(void);
extern int pic8259_register(void);
extern int acpi_mem_mgr_on(void);

int platform_register(void)
{
    pcpu_init();
    pic8259_register();
    apic_register();

    return(0);
}

int platform_init(void)
{
    char *intc_names[] = {"x2APIC","APIC", "8259"};
    char *timer_names[] = {"x2APIC_TIMER","APIC_TIMER","8254"};
    void *intc = NULL;
    
    if(isr_init() != 0)
        return(-1);
    
    if(pagemgr_install_handler() != 0)
        return(-1);

    acpi_mem_mgr_on();
#if 0
    for(int i = 0; i < sizeof(intc_names) / sizeof(char*); i++)
    {
        intc = intc_probe(intc_names[i]);
        
        if(intc != NULL)
        {
            if(intc_init(intc) != 0)
            {
                return(-1);
            }
            kprintf("USING PIC %s\n",intc_names[i]);
            break;
        }
        
    }

    if(intc == NULL)
    {
        return(-1);
    }
#endif
    cpu_init();
}