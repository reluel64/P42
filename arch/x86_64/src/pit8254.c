/* PIT 8259 */


#include <port.h>
#include <devmgr.h>
#include <timer.h>
#include <isr.h>
#include <spinlock.h>
#include <i8254.h>
#include <liballoc.h>
#include <platform.h>
#include <utils.h>

#define COMMAND_PORT 0x43
#define CH0_PORT    0x40


#define PIT8254_MODE_2        (1 << 2)
#define PIT8254_MODE_3       ((1 << 1) | (1 << 2))

#define PIT8254_ACCESS_LO_HI ((1 << 4) | (1 << 5))

#define MODE_MASK             (0b00001110)

#define PIT8254_FREQ             (1193182ull) /* HZ */

#define PIT8254_REQ_RESOLUTION   (1000ull)
#define PIT8254_DIVIDER          (PIT8254_FREQ / PIT8254_REQ_RESOLUTION)


typedef struct pit8254_dev_t
{
    spinlock_rw_t           lock; 
    uint16_t             divider;
    timer_tick_handler_t handler;
    void                *handler_data;
    isr_t                timer_isr;
    uint8_t              mode;
    time_spec_t          resolution;
}pit8254_dev_t;

static int pit8254_irq_handler
(
    void *dev, 
    isr_info_t *inf
);

static int pit8254_rearm(device_t *dev);

static int pit8254_probe(device_t *dev)
{
    if(devmgr_dev_name_match(dev, PIT8254_TIMER) &&
      devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(0);
    }
    
    return(-1);
}

static int pit8254_init(device_t *dev)
{
    pit8254_dev_t *pit_dev = NULL;
  
    pit_dev = (pit8254_dev_t*)kcalloc(sizeof(pit8254_dev_t), 1);
    pit_dev->mode = PIT8254_MODE_2 | PIT8254_ACCESS_LO_HI;
    
    spinlock_rw_init(&pit_dev->lock);

    devmgr_dev_data_set(dev, pit_dev);
    
    pit_dev->divider = 1193;
    pit_dev->resolution.nanosec = (1000000000 / PIT8254_FREQ) * pit_dev->divider ;
    
    kprintf("RESOLUTION %d DIVIDER %d\n",pit_dev->resolution.nanosec, pit_dev->divider);
    __outb(COMMAND_PORT, pit_dev->mode);


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
                        IRQ(0), 
                        0, 
                        &pit_dev->timer_isr);
        }
    }

    return(0);
}

static int pit8254_rearm(device_t *dev)
{
    pit8254_dev_t *pit = NULL;
    
    pit = devmgr_dev_data_get(dev);

    __outb(CH0_PORT, pit->divider & 0xff);
    __outb(CH0_PORT, (pit->divider >> 8) & 0xff);
    
    return(0);
}

static int pit8254_irq_handler
(
    void *dev, 
    isr_info_t *inf
)
{
    pit8254_dev_t *pit_dev = NULL;
    
    pit_dev = devmgr_dev_data_get(dev);

    spinlock_read_lock(&pit_dev->lock);
    
    if(pit_dev->handler != NULL)
    {
        pit_dev->handler(pit_dev->handler_data, &pit_dev->resolution, inf);
    }

    spinlock_read_unlock(&pit_dev->lock);

    return(0);
}

static int pit8254_set_handler
(
    device_t             *dev,
    timer_tick_handler_t th,
    void                 *arg
)
{
    pit8254_dev_t *timer = NULL;
    uint8_t        int_flag = 0;

    timer = devmgr_dev_data_get(dev);

    spinlock_write_lock_int(&timer->lock, &int_flag);

    timer->handler      = th;
    timer->handler_data = arg;

    spinlock_write_unlock_int(&timer->lock, int_flag);

    return(0);
}

static int pit8254_get_handler
(
    device_t             *dev,
    timer_tick_handler_t *th,
    void                 **arg
)
{
    pit8254_dev_t *timer = NULL;
    uint8_t       int_status = 0;
    
    if(th == NULL || arg == NULL)
    {
        return(-1);
    }

    timer = devmgr_dev_data_get(dev);

    spinlock_read_lock_int(&timer->lock, &int_status);

    if(th != NULL)
    {
       *th  = timer->handler;
    }

    if(arg != NULL)
    {
        *arg = timer->handler_data;
    }

    spinlock_read_unlock_int(&timer->lock, int_status);

    return(0);
}

static int pit8254_set_timer
(
    device_t    *dev,
    time_spec_t *tm
)
{
    return(0);
}

static int pit8254_set_mode
(
    device_t *dev,
    uint8_t  mode
)
{
    return(0);
}

static timer_api_t pit8254_api = 
{
    .enable      = NULL,
    .disable     = NULL,
    .reset       = pit8254_rearm,
    .set_handler = pit8254_set_handler,
    .get_handler = pit8254_get_handler,
    .set_timer   = pit8254_set_timer,
    .set_mode    = pit8254_set_mode
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