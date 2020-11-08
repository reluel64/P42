#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <devmgr.h>
#include <cpu.h>
#include <platform.h>
#include <vmmgr.h>

uint32_t cpu_id_get(void)
{
    dev_t *cpu = NULL;
    cpu_api_t *api = NULL;

    /* Get the bootstrap CPU */
    cpu = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, 0);

    api = devmgr_dev_api_get(cpu);

    return(api->cpu_id_get());
}

int cpu_setup(dev_t *dev)
{
    dev_t      *cpu_dev = NULL;
    cpu_api_t  *api = NULL;
    cpu_t      *cpu = NULL;
    uint32_t    cpu_id = 0;
    virt_addr_t stack = 0;
    dev_srch_t  *srch = NULL;

    /* Get the first cpu */

    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);

    api->int_lock();

    cpu_id = api->cpu_id_get();

    cpu = kcalloc(1, sizeof(cpu_t));

    if(cpu == NULL)
        return(-1);
    
    /* Allocate the stack */

    stack =  (virt_addr_t)vmmgr_alloc(NULL, 0x0, 
                                      PER_CPU_STACK_SIZE,
                                      VMM_ATTR_WRITABLE);

    if(!stack)
    {
        kfree(cpu);
        return(-1);
    }

    /* prepare the stack */
    
    memset((void*)stack, 0, PER_CPU_STACK_SIZE);

    cpu->stack_top = stack;

    cpu->stack_bottom = cpu->stack_top + PER_CPU_STACK_SIZE;

    api->stack_relocate(cpu->stack_bottom, _BSP_STACK_BASE);

    /* Clear the old stack */
    memset((void*)_BSP_STACK_TOP, 0, _BSP_STACK_BASE - _BSP_STACK_TOP);

    /* set up some per-cpu stuff */
    pagemgr_per_cpu_init();
    
    if(api->cpu_get_domain)
        cpu->proximity_domain = api->cpu_get_domain(cpu_id);

    api->cpu_setup(cpu);

    api->int_unlock();

    return(0);
}

int cpu_entry_point(void)
{
    return(0);
}

int cpu_int_lock(void)
{
    dev_t     *dev = NULL;
    cpu_api_t *api = NULL;
    uint32_t   cpu_id = 0;
    dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, 0);
    api = devmgr_dev_api_get(dev);

    api->int_lock();

    return(0);
}

int cpu_int_unlock(void)
{
    dev_t     *dev = NULL;
    cpu_api_t *api = NULL;
    uint32_t   cpu_id = 0;
    dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, 0);
    api = devmgr_dev_api_get(dev);

    api->int_unlock();

    return(0);
}

int cpu_int_check(void)
{
    dev_t     *dev = NULL;
    cpu_api_t *api = NULL;
    uint32_t   cpu_id = 0;
    dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, 0);
    api = devmgr_dev_api_get(dev);

    api->int_check();

    return(0);
}