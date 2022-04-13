
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>
#include <platform.h>

typedef struct timer_loop_t
{
    timer_int_t res;
    timer_int_t delay;
    timer_int_t cursor;
}timer_loop_t;

static uint32_t timer_isr_cb
(
    void *tm,
    void *isr_inf
)
{ 
    timer_dev_t *tm_dev = NULL;
    timer_t *c = NULL;
    timer_t *n = NULL;
    uint8_t int_flag = 0;
    tm_dev = tm;

    spinlock_read_lock_int(&tm_dev->queue_lock, &int_flag);

    tm_dev->tm_ticks++;

    c = (timer_t*)linked_list_first(&tm_dev->timer_queue);

    while(c)
    {
        
        n = (timer_t*)linked_list_next(&c->node);

        /* If timer is not initialized, skip it until next cycle */
        if(c->flags & TIMER_NOT_INIT)
        {
            c->flags &= ~TIMER_NOT_INIT;
            c = n;
            continue;
        }

        c->cursor += tm_dev->tm_step;
        /* Do stuff */

        if(c->cursor >= c->delay)
        {
            c->cb(c->arg, isr_inf);
        }
        c = n;
    }

    spinlock_read_unlock_int(&tm_dev->queue_lock, int_flag);

    return(0);
}


static int timer_dev_loop_callback
(
    void *tm_loop,
    void *isr_inf
)
{
    timer_loop_t *loop = NULL;

    loop = tm_loop;

    loop->cursor.nanosec += loop->res.nanosec;
    loop->cursor.seconds += loop->res.seconds;

    if(loop->cursor.nanosec >= 1000000000)
    {
        loop->cursor.seconds += loop->cursor.nanosec / 1000000000;
        loop->cursor.nanosec -= 1000000000;
    }

    return(1);
}

int timer_dev_loop_delay
(
    device_t *dev, 
    uint32_t delay_ms
)
{
    uint32_t      cursor   = 0;
    timer_dev_cb_t  cb       = NULL;
    void         *data     = NULL;

    timer_loop_t loop;

    memset(&loop, 0, sizeof(timer_loop_t));

    loop.delay.nanosec = (uint64_t)delay_ms * 1000000;
    loop.delay.seconds = delay_ms / 1000;
    loop.res.nanosec = 1000000;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(-1);
    }

    /* save the old cb */
    timer_dev_get_cb(dev, &cb, &data);

    /* set the new callback */
    timer_dev_connect_cb(dev, timer_dev_loop_callback, &loop);
    
    /* reset the timer */
    timer_dev_reset(dev);

    while(loop.cursor.seconds < loop.delay.seconds ||
          loop.cursor.nanosec < loop.delay.nanosec)
    {
        cpu_pause();
    }

    /* restore the callback */
    timer_dev_connect_cb(dev, cb, data);
    
    return(0);
}

int timer_dev_get_cb
(
    device_t      *dev,
    timer_dev_cb_t  *cb,
    void         **cb_pv
)
{
    timer_api_t *api = NULL;
    
    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
        return(-1);


    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);
    
    api->get_cb(dev, cb, cb_pv);

    return(0);
}

/* connect timer callback 
 * the callback till be called on every tick of the timer 
 * */

int timer_dev_connect_cb
(
    device_t *dev,
    timer_dev_cb_t cb,
    void *cb_pv
)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
        return(-1);


    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);
    
    api->install_cb(dev, cb, cb_pv);

    return(0);
}

int timer_dev_disconnect_cb
(
    device_t *dev,
    timer_dev_cb_t cb,
    void *cb_pv
)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
        return(-1);


    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);
    
    api->uninstall_cb(dev, cb, cb_pv);

    return(0);
}

int timer_dev_disable(device_t *dev)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
        return(-1);


    api = devmgr_dev_api_get(dev);

    if(api->disable)
        api->disable(dev);

    return(0);
}

int timer_dev_enable(device_t *dev)
{
    timer_api_t *api = NULL;
    
    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
        return(-1);

    api = devmgr_dev_api_get(dev);

    if(api->enable)
        api->enable(dev);

    return(0);
}

int timer_dev_reset(device_t *dev)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
        return(-1);


    api = devmgr_dev_api_get(dev);

    if(api->reset)
        api->reset(dev);

    return(0);
}

int timer_dev_init(timer_dev_t *t, device_t *tm_dev)
{

    timer_dev_cb_t crt_cb = NULL;
    void           *crt_arg = NULL;

    if(t == NULL || tm_dev)
        return(-1);

    linked_list_init(&t->timer_queue);
    spinlock_rw_init(&t->queue_lock);

    /* Check if the tm_dev is a timer device */

    if(!devmgr_dev_type_match(tm_dev, TIMER_DEVICE_TYPE))
        return(-1);

    if(timer_dev_get_cb(tm_dev, &crt_cb, &crt_arg))
        return(-1);

    /* Once the underlying timer device has a queue assigned,
     * do not replace it.
     */
    
    if(crt_cb != NULL)
        return(-1);

    t->timer_dev = tm_dev;

    timer_dev_connect_cb(tm_dev, timer_isr_cb, t);

    return(0);
}

int timer_dev_start
(
    timer_t *t, 
    timer_dev_cb_t cb,
    void *arg,
    uint32_t delay,
    timer_dev_t *tm_dev
)
{
    uint8_t int_flag = 0;
    if(t == NULL || cb == NULL || delay == 0 || tm_dev == NULL)
        return(-1);

    t->cb     = cb;
    t->arg    = arg;
    t->delay  = delay;
    t->cursor = 0;
    t->flags  = TIMER_NOT_INIT;

    /* Protect the queue */
    spinlock_write_lock_int(&tm_dev->queue_lock, &int_flag);
    
    /* Add the timer to the queue */
    linked_list_add_tail(&tm_dev->timer_queue, &t->node);
    
    /* Release the queue */
    spinlock_write_unlock_int(&tm_dev->queue_lock, int_flag);

    return(0);
}