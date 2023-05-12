#include <defs.h>
#include <devmgr.h>
#include <timer.h>
#include <apic_timer.h>
#include <liballoc.h>
#include <linked_list.h>
#include <spinlock.h>
#include <isr.h>
#include <i8254.h>
#include <cpu.h>
#include <platform.h>
#include <utils.h>

typedef struct apic_timer_t
{
    spinlock_t           lock;
    uint32_t             calib_value;
    timer_tick_handler_t handler;
    void                *handler_data;
    time_spec_t          tm_res;
}apic_timer_t;

static isr_t timer_isr;

static int apic_timer_isr(void *drv, isr_info_t *inf)
{
    apic_timer_t  *timer     = NULL;
    device_t      *dev = NULL;
    uint8_t       int_flag = 0;

    dev = devmgr_dev_get_by_name(APIC_TIMER_NAME, inf->cpu_id);

    if(dev == NULL)
    {
        return(-1);
    }
    
    timer = devmgr_dev_data_get(dev);

    if(timer == NULL)
    {
        return(-1);
    }

    spinlock_read_lock(&timer->lock);
    
    /* Call the callback */
    if(timer->handler != NULL)
    {
        timer->handler(timer->handler_data, &timer->tm_res, inf);
    }

    spinlock_read_unlock(&timer->lock);
 
    return(0);
}

static int apic_timer_probe(device_t *dev)
{
    if(devmgr_dev_name_match(dev, APIC_TIMER_NAME) && 
       devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(0);
    }

    return(-1);
}

static uint32_t apic_timer_loop
(
    void *arg,
    const void *isr
)
{
    uint8_t *flag = NULL;

    flag = arg;

    if(flag != NULL)
    {
        (*flag) = 1;
    }

    return(0);
}

static int apic_timer_init(device_t *dev)
{  
    device_t           *apic_dev    = NULL;
    driver_t           *apic_drv    = NULL;
    apic_device_t      *apic        = NULL;
    apic_drv_private_t *apic_drv_pv = NULL;
    apic_timer_t       *apic_timer  = NULL;
    device_t           *pit         = NULL;
    uint32_t            data        = 0;
    timer_api_t        *api         = NULL;
    int                int_status   = 0;
    timer_t            calib_timer;
    time_spec_t        req_res      = {.nanosec = 1000000, .seconds = 0};
    uint8_t            timer_done   = 0;


    apic_dev    = devmgr_dev_parent_get(dev);
    apic_drv    = devmgr_dev_drv_get(apic_dev);
    apic_drv_pv = devmgr_drv_data_get(apic_drv);

    apic_timer  = kcalloc(sizeof(apic_timer_t), 1);
    
    if(apic_timer == NULL)
    {
        return(-1);
    }

    /* Save the interrupt flag */
    int_status = cpu_int_check();

    /* Enable the interrupts */

    if(!int_status)
    {
        cpu_int_unlock();
    }
    spinlock_rw_init(&apic_timer->lock);

    devmgr_dev_data_set(dev, apic_timer);

    pit = devmgr_dev_get_by_name(PIT8254_TIMER, 0);
    api = devmgr_dev_api_get(pit);

    data = 0b1011;
    apic_drv_pv->apic_write(apic_drv_pv->vaddr, 
                            DIVIDE_CONFIGURATION_REGISTER, 
                            &data, 
                            1);

    /* Let's calibrate this */
    data = UINT32_MAX;
    apic_drv_pv->apic_write(apic_drv_pv->vaddr, 
                            INITIAL_COUNT_REGISTER, 
                            &data, 
                            1);

    timer_enqeue_static(NULL, 
                        &req_res, 
                        apic_timer_loop, 
                        &timer_done, 
                        &calib_timer);

    while(!timer_done)
    {
        cpu_pause();
    }

    /* restore the status of the interrupt flag */
    if(!int_status) 
    {
        cpu_int_lock();
    }

    apic_drv_pv->apic_read(apic_drv_pv->vaddr, 
                           CURRENT_COUNT_REGISTER, 
                           &data, 
                           1);

    apic_timer->calib_value = UINT32_MAX - data;
 
     /* disable the timer */
    data = apic_timer->calib_value;

    apic_drv_pv->apic_write(apic_drv_pv->vaddr, 
                            INITIAL_COUNT_REGISTER, 
                            &data, 
                            1);

    apic_timer->tm_res = req_res;
    kprintf("APIC_TIMER_CALIB %d SEC %d NSEC %d\n",
            apic_timer->calib_value,
            req_res.seconds,
            req_res.nanosec);

    data = APIC_LVT_VECTOR_MASK(PLATFORM_LOCAL_TIMER_VECTOR) | 
           0b01 << 17;

    apic_drv_pv->apic_write(apic_drv_pv->vaddr, 
                            LVT_TIMER_REGISTER, 
                            &data, 
                            1);
    return(0);
}

static int apic_timer_drv_init(driver_t *drv)
{
    isr_install(apic_timer_isr, 
                drv, 
                PLATFORM_LOCAL_TIMER_VECTOR, 
                0, 
                &timer_isr);

    return(0);
}

static int apic_timer_set_handler
(
    device_t             *dev,
    timer_tick_handler_t  th, 
    void                 *arg
)
{
    apic_timer_t *timer = NULL;
    uint8_t       int_flag = 0;

    timer = devmgr_dev_data_get(dev);

    spinlock_write_lock_int(&timer->lock, &int_flag);

    timer->handler = th;
    timer->handler_data = arg;
    
    spinlock_write_unlock_int(&timer->lock, int_flag);

    return(0);
}

static int apic_timer_get_handler
(
    device_t             *dev,
    timer_tick_handler_t *th,
    void                **arg
)
{
    apic_timer_t *timer = NULL;
    uint8_t       int_flag = 0;


    if((th == NULL) || (arg == NULL))
    {
        return(-1);
    }

    timer = devmgr_dev_data_get(dev);

    spinlock_read_lock_int(&timer->lock, &int_flag);

    if(th != NULL)
    {
        *th = timer->handler;
    }

    if(arg != NULL)
    {
        *arg = timer->handler_data;
    }

    spinlock_read_unlock_int(&timer->lock, int_flag);

    return(0);
}

static int apic_timer_toggle(device_t *dev, int en)
{
    device_t           *apic_dev    = NULL;
    driver_t           *apic_drv    = NULL;
    apic_device_t      *apic        = NULL;
    apic_drv_private_t *apic_drv_pv = NULL;
    apic_timer_t       *apic_timer  = NULL;
    uint32_t            data        = 0;

    apic_dev    = devmgr_dev_parent_get(dev);
    apic_drv    = devmgr_dev_drv_get(apic_dev);
    apic_drv_pv = devmgr_drv_data_get(apic_drv);
    apic_timer  = devmgr_dev_data_get(dev);

    if(en)
    {
        data = apic_timer->calib_value;
    }
    
    apic_drv_pv->apic_write(apic_drv_pv->vaddr, 
                            INITIAL_COUNT_REGISTER, 
                            &data, 
                            1);
    return(0);
}

static int apic_timer_enable(device_t *dev)
{
    return(apic_timer_toggle(dev, 1));
}

static int apic_timer_disable(device_t *dev)
{
    return(apic_timer_toggle(dev, 0));
}

static int apic_timer_reset(device_t *dev)
{
    return(apic_timer_toggle(dev, 1));
}

static int apic_timer_resolution_get
(
    device_t    *dev, 
    time_spec_t *tm_res
)
{
    apic_timer_t *timer = NULL;

    timer = devmgr_dev_data_get(dev);

    if(timer == NULL || tm_res == NULL)
        return(-1);

    memcpy(tm_res, &timer->tm_res, sizeof(time_spec_t));

    return(0);
}

static timer_api_t apic_timer_api = 
{
    .enable       = apic_timer_enable,
    .disable      = apic_timer_disable,
    .reset        = apic_timer_reset,
    .set_handler  = apic_timer_set_handler,
    .get_handler  = apic_timer_get_handler
};

static driver_t apic_timer_drv = 
{
    .drv_name   = APIC_TIMER_NAME,
    .drv_type   = TIMER_DEVICE_TYPE,
    .dev_probe  = apic_timer_probe,
    .dev_init   = apic_timer_init,
    .dev_uninit = NULL,
    .drv_init   = apic_timer_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &apic_timer_api
};

int apic_timer_register(void)
{
    devmgr_drv_add(&apic_timer_drv);
    devmgr_drv_init(&apic_timer_drv);
    return(0);
}