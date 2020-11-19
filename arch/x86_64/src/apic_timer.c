#include <defs.h>
#include <devmgr.h>
#include <timer.h>
#include <apic_timer.h>


static int apic_timer_probe(void)
{
    return(0);
}

static int apic_timer_init(void)
{

    return(0);
}

static int apic_timer_drv_init(void)
{
    return(0);
}

static timer_api_t apic_timer_api = 
{
    .arm_timer = NULL,
    .disarm_timer = NULL
};

static driver_t apic_timer = 
{
    .drv_name = APIC_TIMER_NAME,
    .drv_type = TIMER_DEVICE_TYPE,
    .dev_probe = apic_timer_probe,
    .dev_init  = apic_timer_init,
    .dev_uninit = NULL,
    .drv_init   = apic_timer_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &apic_timer_api
};

int apic_timer_register(void)
{
    devmgr_drv_add(&apic_timer);
    devmgr_drv_init(&apic_timer);
    return(0);
}