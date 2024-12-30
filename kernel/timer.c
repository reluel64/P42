
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>
#include <platform.h>


/* local variables */
static struct timer_device system_timer = {0};

static uint8_t timer_increment
(
    struct time_spec       *cursor,
    const struct time_spec *step
)
{
    /* check if we are overflowing the nanoseconds
     * if we do, increase the seconds and calculate the difference
     * of the nanoseconds
     */
    if(step->nanosec + cursor->nanosec >= 1000000000ull)
    {
        cursor->seconds++;
        cursor->nanosec = (cursor->nanosec + step->nanosec) - 1000000000ull;
    }
    else
    {
        cursor->nanosec += step->nanosec;
    }

    /* increment seconds */
    cursor->seconds += step->seconds;

    return(0);
}

static uint8_t timer_expired
(
    struct time_spec *target,
    struct time_spec *current
)
{
    uint8_t expired = 0;

    /* Check if we went past the required seconds */
    if(target->seconds <= current->seconds)
    {
        if(target->seconds == current->seconds)
        {
            /* If we reached the target seconds, then check the nanoseconds */
            if(target->nanosec <= current->nanosec)
            {
                expired = 1;
            }
        }
        else
        {
            expired = 1;
        }
    }

    return(expired);
}

static void timer_compute_earliest_deadline
(
    struct time_spec *ed,
    const struct time_spec *deadline,
    const struct time_spec *cursor
)
{
    
    ed->seconds = deadline->seconds - cursor->seconds;
    ed->nanosec = deadline->nanosec - cursor->nanosec;
}

static uint32_t timer_queue_callback
(
    void        *tm,
    const struct time_spec *step,
    const void        *isr_inf
)
{ 
    struct timer_device *tm_dev  = NULL;
    struct timer     *c       = NULL;
    struct timer     *n       = NULL;
    uint8_t     status   = 0;

    tm_dev = tm;
    
    spinlock_lock(&tm_dev->lock_active_q);
   static uint32_t  prev_sec = 0;
    /* increment our timer */
    timer_increment(&tm_dev->current_increment, step);

    if(prev_sec != tm_dev->current_increment.seconds)
    {
        prev_sec = tm_dev->current_increment.seconds;
    }


    c = (struct timer*)linked_list_first(&tm_dev->active_q);

    while(c != NULL)
    {
        n = (struct timer*)linked_list_next(&c->node);

        timer_increment(&c->cursor, step);

        status = timer_expired(&c->to_sleep, &c->cursor);

        /* Do stuff */

        if(status)
        {    

            c->callback(c, c->arg, isr_inf);
            /* if the timer was one-shot, then remove it from the list */
            if(c->flags & TIMER_PERIODIC)
            {
                memset(&c->cursor, 0, sizeof(struct time_spec));
            }
            else
            {
                linked_list_remove(&tm_dev->active_q, &c->node);
                c->flags |= TIMER_PROCESSED;
            }
        }

        c = n;
    }

    spinlock_lock(&tm_dev->lock_pend_q);
    
    linked_list_concat(&tm_dev->pend_q, &tm_dev->active_q);
    
    spinlock_unlock(&tm_dev->lock_pend_q);

    spinlock_unlock(&tm_dev->lock_active_q);

    return(0);
}

int timer_set_system_timer
(
    struct device_node *dev
)
{
    uint8_t     int_flag = 0;
    struct timer_api *func = NULL;

    if(dev == NULL)
    {
        return(-1);
    }

    func = devmgr_dev_api_get(dev);

    if(func == NULL || func->set_handler == NULL)
    {
        return(-1);
    }

    spinlock_lock_int(&system_timer.lock_active_q, &int_flag);

    system_timer.backing_dev = dev;
    func->set_handler(dev, timer_queue_callback, &system_timer);

    spinlock_unlock_int(&system_timer.lock_active_q, int_flag);

    return(0);
}

int timer_system_init(void)
{
    memset(&system_timer, 0, sizeof(struct timer_device));
    linked_list_init(&system_timer.active_q);
    linked_list_init(&system_timer.pend_q);
    spinlock_init(&system_timer.lock_active_q);
    spinlock_init(&system_timer.lock_pend_q);
    return(0);
}


int timer_enqeue
(
    struct timer_device     *timer_dev,
    struct time_spec     *ts,
    timer_handler_t func,
    void            *arg,
    uint8_t         flags
)
{
    uint8_t int_sts = 0;
    struct timer_device *tmd = NULL;
    struct timer     *tm = NULL;

    if(timer_dev == NULL)
    {
        tmd = &system_timer;
    }
    else
    {
        tmd = timer_dev;
    }
    
    if(func == NULL || ts == NULL)
    {
        return(-1);
    }

    tm = (struct timer*)kcalloc(sizeof(struct timer), 1);

    tm->arg = arg;
    tm->callback = func;
    tm->to_sleep = *ts;
    tm->flags = flags;
    spinlock_lock_int(&tmd->lock_pend_q, &int_sts);

    linked_list_add_tail(&tmd->pend_q, &tm->node);
    
    spinlock_unlock_int(&tmd->lock_pend_q, int_sts);
    
    return(0);
}

int timer_enqeue_static
(
    struct timer_device     *timer_dev,
    struct time_spec     *ts,
    timer_handler_t func,
    void            *arg,
    uint8_t         flags,
    struct timer         *tm
)
{
    uint8_t int_sts = 0;
    struct timer_device *tmd = NULL;

    if(timer_dev == NULL)
    {
        tmd = &system_timer;
    }
    else
    {
        tmd = timer_dev;
    }
    
    if(tm == NULL || func == NULL || ts == NULL)
    {
        kprintf("INVALID PARAMS %x %x %x\n",tm, func,ts);
        return(-1);
    }
    
    memset(tm, 0, sizeof(struct timer));

    tm->arg = arg;
    tm->callback = func;
    tm->to_sleep = *ts;
    tm->flags = flags;
    spinlock_lock_int(&tmd->lock_pend_q, &int_sts);   

    linked_list_add_tail(&tmd->pend_q, &tm->node);
    
    spinlock_unlock_int(&tmd->lock_pend_q, int_sts);
    
    return(0);
}


int timer_dequeue
(
    struct timer_device *timer_dev,
    struct timer *tm
)
{
    uint8_t     int_sts = 0;
    struct timer_device *tmd = NULL;
    int found = 0;
    
    if(timer_dev == NULL)
    {
        tmd = &system_timer;
    }
    else
    {
        tmd = timer_dev;
    }

    if((tmd == NULL) || (tm == NULL))
    {
        return(-1);
    }

    if(tm->flags & TIMER_PROCESSED)
    {
        return(0);
    }
    
    int_sts = cpu_int_check();

    if(int_sts)
    {
        cpu_int_lock();
    }

    /* Look into the active queue */
    spinlock_lock(&tmd->lock_active_q);

    if(linked_list_find_node(&tmd->active_q, &tm->node) == 0)
    {
        linked_list_remove(&tmd->active_q, &tm->node);
        found = 1;
    }

    spinlock_unlock(&tmd->lock_active_q);

    if(found == 0)
    {
        /* Look into the pend queue */
        spinlock_lock(&tmd->lock_pend_q);

        if(linked_list_find_node(&tmd->pend_q, &tm->node) == 0)
        {
            linked_list_remove(&tmd->pend_q, &tm->node);
            found = 1;
        }

        spinlock_unlock(&tmd->lock_pend_q);
    }
    
    if(int_sts)
    {
        cpu_int_unlock();
    }
    
    if(found == 1)
    {
        return(0);
    }

    return(-1);
}