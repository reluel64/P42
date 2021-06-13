/* PIT 8259 */


#include <port.h>
#include <devmgr.h>
#include <timer.h>
#include <isr.h>
#include <spinlock.h>
#include <i8254.h>
#include <liballoc.h>
#include <platform.h>

#define COMMAND_PORT 0x43
#define CH0_PORT    0x40

#define INTERRUPT_INTERVAL_MS 1ull

#define PIT8254_FREQ 1193182 /* HZ */
#define INT_INTERVAL_MS 1
#define PIT8254_MS_DIV 1000


typedef struct pit8254_dev_t
{
    list_head_t queue;
    spinlock_t  lock; 
    uint16_t divider;
    timer_dev_cb func;
    void *func_data;
    isr_t timer_isr;
}pit8254_dev_t;

static int pit8254_rearm(device_t *dev)
{
    pit8254_dev_t *pit = NULL;
    
    pit = devmgr_dev_data_get(dev);

    __outb(CH0_PORT, pit->divider & 0xff);
    __outb(CH0_PORT, (pit->divider >> 8) & 0xff);

    return(0);
}

static int pit8254_irq_handler(void *dev, isr_info_t *inf)
{
    pit8254_dev_t *pit_dev = NULL;
    uint16_t divider = 0;
    int int_status = 0;

    pit_dev = devmgr_dev_data_get(dev);


    spinlock_lock_int(&pit_dev->lock, &int_status);
    vga_print("PIT8254\n");
    if(pit_dev->func != NULL)
        pit_dev->func(pit_dev->func_data, INT_INTERVAL_MS, inf);
    
    spinlock_unlock_int(&pit_dev->lock, int_status);

    return(0);
}

static int pit8254_probe(device_t *dev)
{
    if(devmgr_dev_name_match(dev, PIT8254_TIMER) &&
      devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
      return(0);

    return(-1);
}

static int pit8254_init(device_t *dev)
{
    uint8_t        command = 0;
    pit8254_dev_t *pit_dev = NULL;

    command = 0b000111000;

    pit_dev = (pit8254_dev_t*)kcalloc(sizeof(pit8254_dev_t), 1);

    spinlock_init(&pit_dev->lock);
    devmgr_dev_data_set(dev, pit_dev);
    
    pit_dev->divider = PIT8254_FREQ / PIT8254_MS_DIV;

    __outb(COMMAND_PORT, command);

    pit8254_rearm(dev);

    return(0);
}

static int pit8254_drv_init(driver_t *drv)
{
    device_t *dev = NULL;
    pit8254_dev_t *pit_dev = NULL;

    if(!devmgr_dev_create(&dev))
    {
        devmgr_dev_name_set(dev, PIT8254_TIMER);
        devmgr_dev_type_set(dev, TIMER_DEVICE_TYPE);
        devmgr_dev_index_set(dev, 0);
        
        if(!devmgr_dev_add(dev, NULL))
        {
            pit_dev = devmgr_dev_data_get(dev);
            isr_install(pit8254_irq_handler, 
                        dev, 
                        IRQ0, 
                        0, 
                        &pit_dev->timer_isr);
        }
    }

    return(0);
}



static int pit8254_install_cb
(
    device_t          *dev,
    timer_dev_cb      func, 
    void              *data
)
{
    pit8254_dev_t *timer = NULL;
    int           int_status = 0;
    int           ret = -1;

    timer = devmgr_dev_data_get(dev);

    spinlock_lock_int(&timer->lock, &int_status);

    timer->func      = func;
    timer->func_data = data;
    ret = 0;

    spinlock_unlock_int(&timer->lock, int_status);

    return(ret);
}

static int pit8254_uninstall_cb
(
    device_t          *dev,
    timer_dev_cb func,
    void *data
)
{
    pit8254_dev_t *timer = NULL;
    int           int_status = 0;
    int           ret = -1;
    
    timer = devmgr_dev_data_get(dev);

    spinlock_lock_int(&timer->lock, &int_status);

    timer->func      = NULL;
    timer->func_data = NULL;
    ret              = 0;

    spinlock_unlock_int(&timer->lock, int_status);

    return(ret);
}

static int pit8254_get_cb
(
    device_t          *dev,
    timer_dev_cb      *func,
    void              **data
)
{
    pit8254_dev_t *timer = NULL;
    int           int_status = 0;
    int           ret = -1;
    
    timer = devmgr_dev_data_get(dev);

    spinlock_lock_int(&timer->lock, &int_status);

    *func = timer->func;
    *data = timer->func_data;
    ret = 0;

    spinlock_unlock_int(&timer->lock, int_status);

    return(ret);
}

static timer_api_t pit8254_api = 
{
    .install_cb    = pit8254_install_cb,
    .uninstall_cb  = pit8254_uninstall_cb,
    .get_cb        = pit8254_get_cb,
    .enable        = NULL,
    .disable       = NULL,
    .reset         = pit8254_rearm
};

static driver_t pit8254 = 
{
    .drv_name  = PIT8254_TIMER,
    .drv_type  = TIMER_DEVICE_TYPE,
    .dev_probe = pit8254_probe,
    .dev_init  = pit8254_init,
    .drv_init  = pit8254_drv_init,
    .drv_api   = &pit8254_api
};

int pit8254_register(void)
{
    devmgr_drv_add(&pit8254);
    devmgr_drv_init(&pit8254);
    
    return(0);
}