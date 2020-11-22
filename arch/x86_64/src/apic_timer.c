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

#define APIC_TIMER_INTERVAL_MS 10

typedef struct apic_timer_t
{
    list_head_t queue;
    spinlock_t lock;
    volatile apic_reg_t *reg;
    uint32_t calib_value;
}apic_timer_t;

static int apic_timer_isr(void *dev, virt_addr_t iframe)
{
    int                 int_status = 0;
    apic_timer_t        *timer     = NULL;
    volatile apic_reg_t *reg       = NULL;

    if(devmgr_dev_index_get(dev) != cpu_id_get())
    {
        return(-1);
    }
    
    timer = devmgr_dev_data_get(dev);
    reg = timer->reg;

    spinlock_lock_interrupt(&timer->lock, &int_status);

    if(linked_list_count(&timer->queue) > 0)
        timer_update(&timer->queue, APIC_TIMER_INTERVAL_MS);

    (*reg->timer_icnt) = timer->calib_value;

    spinlock_unlock_interrupt(&timer->lock, int_status);

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

static int apic_timer_init(device_t *dev)
{
    device_t           *apic_dev    = NULL;
    driver_t           *apic_drv    = NULL;
    apic_device_t      *apic        = NULL;
    apic_drv_private_t *apic_drv_pv = NULL;
    apic_timer_t       *apic_timer  = NULL;
    device_t           *pit         = NULL;
    volatile apic_reg_t *reg = NULL;
 
    apic_dev    = devmgr_dev_parent_get(dev);
    apic_drv    = devmgr_dev_drv_get(apic_dev);
    apic_drv_pv = devmgr_drv_data_get(apic_drv);

    apic_timer  = kcalloc(sizeof(apic_timer_t), 1);
    
    if(apic_timer == NULL)
        return(-1);

    apic_timer->reg = apic_drv_pv->reg;
    reg = apic_timer->reg;

    spinlock_init(&apic_timer->lock);
    linked_list_init(&apic_timer->queue);

    devmgr_dev_data_set(dev, apic_timer);

    pit = devmgr_dev_get_by_name(PIT8254_TIMER, 0);

    (*reg->timer_div) = 0b1011;

    /* Let's calibrate this */
    (*reg->timer_icnt) = UINT32_MAX;

    timer_loop_delay(pit, APIC_TIMER_INTERVAL_MS);

    apic_timer->calib_value = UINT32_MAX - (*reg->timer_ccnt);

    kprintf("APIC_TIMER_CALIB %d\n",apic_timer->calib_value);

    (*reg->lvt_timer) = APIC_LVT_VECTOR_MASK(82) | 0b01 << 17;

    (*reg->timer_icnt) = apic_timer->calib_value;

    isr_install(apic_timer_isr, dev, 82, 0);
    return(0);
}

static int apic_timer_drv_init(driver_t *drv)
{
    return(0);
}

static int apic_timer_arm(device_t *dev, timer_t *tm)
{
    apic_timer_t *timer      = NULL;
    int           int_status = 0;

    timer = devmgr_dev_data_get(dev);

    spinlock_lock_interrupt(&timer->lock, &int_status);
    linked_list_add_tail(&timer->queue, &tm->node);
    spinlock_unlock_interrupt(&timer->lock, int_status);

    return(0);
}

static int apic_timer_disarm(device_t *dev, timer_t *tm)
{
    return(0);
}


static timer_api_t apic_timer_api = 
{
    .arm_timer = apic_timer_arm,
    .disarm_timer = NULL
};

static driver_t apic_timer = 
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
    devmgr_drv_add(&apic_timer);
    devmgr_drv_init(&apic_timer);
    return(0);
}