
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>
#include <platform.h>

static int timer_compare
(
    timer_interval_t *t1,
    timer_interval_t *t2
)
{
    if(t1->seconds > t2->seconds)
    {
        return(1);
    }
    else if(t1->seconds < t2->seconds)
    {
        return(-1);
    }
    else
    {
        if(t1->nanosec > t2->nanosec)
        {
            return(2);
        }
        else if(t1->nanosec < t2->nanosec)
        {
            return(-2);
        }
    }

    return(0);
}

static uint8_t timer_increment
(
    timer_interval_t *timer,
    timer_interval_t *increment
)
{
    /* check if we are overflowing the nanoseconds
     * if we do, increase the seconds and calculate the difference
     * of the nanoseconds
     */
    if(increment->nanosec + timer->nanosec >= 1000000000ull)
    {
        increment->seconds++;
        timer->nanosec = increment->nanosec;
    }
    else
    {
        timer->nanosec += increment->nanosec;
    }

    /* increment seconds */
    timer->seconds += increment->seconds;

    return(0);
}

static uint8_t timer_expired
(
    timer_interval_t *to_sleep,
    timer_interval_t *cursor
)
{
    /* Check if we went past the required seconds */
    if(to_sleep->seconds <= cursor->seconds)
    {
        /* If we reached the target seconds, then check the nanoseconds */
        if(to_sleep->nanosec <= cursor->nanosec)
        {
            return(1);
        }
    }

    return(0);
}

static uint32_t timer_queue_callback
(
    void *tm,
    void *isr_inf
)
{ 
    timer_dev_t *tm_dev  = NULL;
    timer_t     *c       = NULL;
    timer_t     *n       = NULL;
    uint8_t     int_flag = 0;
    uint8_t     status   = 0;

    tm_dev = tm;

    spinlock_lock_int(&tm_dev->lock_q, &int_flag);

    /* increment our timer */
    timer_increment(&tm_dev->current_increment, &tm_dev->step);

    /* if the smallest timer did not expire, just bail out */
    if(!timer_expired(&tm_dev->next_increment, &tm_dev->current_increment))
    {
        spinlock_unlock_int(&tm_dev->lock_q, int_flag);
        return(0);
    }

    c = (timer_t*)linked_list_first(&tm_dev->timer_q);

    while(c != NULL)
    {
        n = (timer_t*)linked_list_next(&c->node);

        /* If timer is not initialized, skip it until next cycle */
        if(c->flags & TIMER_NOT_INIT)
        {
            c->flags &= ~TIMER_NOT_INIT;
            c = n;
            continue;
        }

        timer_increment(&c->cursor, &tm_dev->next_increment);

        status = timer_expired(&c->to_sleep, &c->cursor);

        /* Do stuff */

        if(status)
        {
            status = c->callback(c->arg, isr_inf, &c->to_sleep);

            if(status == 0)
            {
                linked_list_remove(&tm_dev->timer_q, &c->node);
            }
            else
            {
                c->cursor.nanosec = 0;
                c->cursor.seconds = 0;
            }
        }

        c = n;
    }

    spinlock_unlock_int(&tm_dev->lock_q, int_flag);

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
    {
        return(-1);
    }

    api = devmgr_dev_api_get(dev);

    if(api == NULL)
    {
        return(-1);
    }

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
    {
        return(-1);
    }

    api = devmgr_dev_api_get(dev);

    if(api == NULL)
    {
        return(-1);
    }

    api->uninstall_cb(dev, cb, cb_pv);

    return(0);
}

int timer_dev_disable(device_t *dev)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(-1);
    }

    api = devmgr_dev_api_get(dev);

    if(api->disable)
    {
        api->disable(dev);
    }

    return(0);
}

int timer_dev_enable(device_t *dev)
{
    timer_api_t *api = NULL;
    
    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(-1);
    }

    api = devmgr_dev_api_get(dev);

    if(api->enable)
    {
        api->enable(dev);
    }

    return(0);
}

int timer_dev_reset(device_t *dev)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(-1);
    }

    api = devmgr_dev_api_get(dev);

    if(api->reset)
    {
        api->reset(dev);
    }

    return(0);
}

int timer_dev_read
(
    device_t *dev,
    uint64_t *val
)
{
    timer_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(-1);
    }

    api = devmgr_dev_api_get(dev);

    if(api->read_timer)
    {
        api->read_timer(dev, val);
    }

    return(0);
}

int timer_dev_init
(
    timer_dev_t *timer_dev, 
    device_t    *dev
)
{
    if((timer_dev == NULL) || (dev == NULL))
    {
        return(-1);
    }

    linked_list_init(&timer_dev->timer_q);
    spinlock_init(&timer_dev->lock_q);

    /* Check if the tm_dev is a timer device */

    if(!devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(-1);
    }

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

    t->callback = cb;
    t->arg      = arg;
    t->delay    = delay;
    t->cursor   = 0;
    t->flags    = TIMER_NOT_INIT;

    memset(&t->cursor, 0, sizeof(timer_interval_t));
    
    /* Protect the queue */
    spinlock_write_lock_int(&tm_dev->lock_q, &int_flag);
    
    /* Add the timer to the queue */
    linked_list_add_tail(&tm_dev->timer_q, &t->node);
    
    /* Release the queue */
    spinlock_write_unlock_int(&tm_dev->lock_q, int_flag);

    return(0);
}
