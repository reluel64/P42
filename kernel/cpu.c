
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <devmgr.h>
#include <cpu.h>
#include <platform.h>
#include <vmmgr.h>
#include <intc.h>

extern int pcpu_api_register(cpu_api_t **api);
extern int pcpu_init(void);

static cpu_api_t *api = NULL;


int cpu_api_register(void)
{
    pcpu_api_register(&api);
}

int cpu_init(void)
{
    pcpu_init();
}

uint32_t cpu_id_get(void)
{

    return(api->cpu_id_get());
}

int cpu_setup(device_t *dev)
{
    device_t   *cpu_dev = NULL;
    cpu_t      *cpu = NULL;
    uint32_t    cpu_id = 0;
    virt_addr_t stack = 0;
    dev_srch_t  *srch = NULL;

    /* Get the first cpu */

    if(api == NULL)
        return(-1);

    api->int_lock();

    /* set up some per-cpu stuff */
    pagemgr_per_cpu_init();

    cpu_id = api->cpu_id_get();

    cpu = kcalloc(1, sizeof(cpu_t));

    if(cpu == NULL)
        return(-1);
    
    /* Allocate the stack */

    stack = (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                      PER_CPU_STACK_SIZE,
                                      VMM_ATTR_WRITABLE);

    if(!stack)
    {
        kfree(cpu);
        return(-1);
    }

    cpu->dev = dev;
    
    devmgr_dev_data_set(dev, cpu);
    /* prepare the stack */
    
    memset((void*)stack, 0, PER_CPU_STACK_SIZE);

    cpu->stack_top = stack;

    cpu->stack_bottom = cpu->stack_top + PER_CPU_STACK_SIZE;

    /* ask platform code to do the stack relocation */
    api->stack_relocate((virt_addr_t*)cpu->stack_bottom, 
                        (virt_addr_t*)_BSP_STACK_BASE);

    /* Clear the old stack */
    
    cpu->cpu_id = api->cpu_id_get();
    
    if(api->cpu_get_domain)
        cpu->proximity_domain = api->cpu_get_domain(cpu_id);

    /* Perform platform specific cpu setup */
    api->cpu_setup(cpu);

    memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);

    api->int_unlock();

    return(0);
}

int cpu_int_lock(void)
{
    api->int_lock();
    return(0);
}

int cpu_int_unlock(void)
{
    api->int_unlock();
    return(0);
}

int cpu_int_check(void)
{
    return(api->int_check());
}

int cpu_ap_start(uint32_t count, uint32_t timeout)
{
    return(api->start_ap(count, timeout));
}

virt_addr_t cpu_virt_max(void)
{
    return(api->max_virt_addr());
}

phys_addr_t cpu_phys_max(void)
{
    return(api->max_phys_addr());
}

int cpu_issue_ipi
(
    uint8_t dest, 
    uint32_t cpu,
    uint32_t type
)
{
    uint32_t vector = 0xFF;

    switch(type)
    {
        case IPI_RESCHED:
            vector = PLATFORM_RESCHED_VECTOR;
            break;

        case IPI_INVLPG:
            vector = PLATFORM_PG_INVALIDATE_VECTOR;
            break;

        default:
            vector = type;
            break;
    }
    
    return(api->ipi_issue(dest, cpu, vector));
}

void cpu_halt(void)
{
    api->halt();
}

void cpu_pause(void)
{
    api->pause();
}

