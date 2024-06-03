#ifndef timerh
#define timerh

#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#include <devmgr.h>
#include <isr.h>
#include <stdint.h>


#define TIMER_DEVICE_TYPE "timer"
#define TIMER_NOT_INIT    (1 << 0)
#define TIMER_PERIODIC    (1 << 0)
#define TIMER_ONESHOT     (1 << 1)

#define TIMER_RESOLUTION_NS  (1000000000ull) // nanosecond
#define NODE_TO_TIMER (node)    ((uint8_t*)(node) - offsetof((node), timer_t))


typedef struct time_spec_t
{
    uint32_t nanosec;
    uint32_t seconds;
}time_spec_t;


typedef uint32_t (*timer_tick_handler_t) \
                 (void *arg, const time_spec_t *step, const void *isr_inf);

typedef uint32_t (*timer_handler_t) \
                 (void *arg, const void *isr_inf);


typedef struct timer_dev_t
{
    device_t    *backing_dev;  /* backing timer device      */
    list_head_t active_q;      /* timer queue               */
    list_head_t pend_q;
    spinlock_t  lock_active_q;       /* lock to ptorect the queue */
    spinlock_t  lock_pend_q; 
    time_spec_t step;
    time_spec_t next_increment;
    time_spec_t current_increment;
}timer_dev_t;

typedef struct timer_t
{
    list_node_t       node;
    timer_handler_t   callback;
    void              *arg;
    time_spec_t       to_sleep;
    time_spec_t       cursor;
    uint8_t           flags;
}timer_t;

typedef struct timer_api_t
{
    int (*enable)      (device_t *dev);
    int (*disable)     (device_t *dev);
    int (*reset)       (device_t *dev);
    int (*set_handler) (device_t *dev, timer_tick_handler_t th, void *arg);
    int (*get_handler) (device_t *dev, timer_tick_handler_t *th, void **arg);
    int (*set_timer)   (device_t *dev, time_spec_t *tm);
    int (*set_mode)    (device_t *dev, uint8_t mode); 
}timer_api_t; 

int timer_set_system_timer
(
    device_t *dev
);

int timer_system_init
(
    void
);

int timer_enqeue
(
    timer_dev_t     *timer_dev,
    time_spec_t     *ts,
    timer_handler_t func,
    void            *arg
);

int timer_enqeue_static
(
    timer_dev_t     *timer_dev,
    time_spec_t     *ts,
    timer_handler_t func,
    void            *arg,
    timer_t         *tm
);

int timer_dequeue
(
    timer_dev_t *timer_dev,
    timer_t *tm
);

#endif