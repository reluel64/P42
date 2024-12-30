#ifndef timerh
#define timerh

#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#include <devmgr.h>
#include <isr.h>
#include <stdint.h>


#define TIMER_DEVICE_TYPE "timer"
#define TIMER_PERIODIC    (1 << 0)
#define TIMER_ONESHOT     (1 << 1)
#define TIMER_PROCESSED   (1 << 2)
#define TIMER_RESOLUTION_NS  (1000000000ull) // nanosecond
#define NODE_TO_TIMER (node)    ((uint8_t*)(node) - offsetof((node), struct timer))

struct time_spec
{
    uint32_t nanosec;
    uint32_t seconds;
};

struct timer;

typedef uint32_t (*timer_tick_handler_t) \
                 (void *arg, const struct time_spec *step, const void *isr_inf);

typedef uint32_t (*timer_handler_t) \
                 (struct timer *tm, void *arg, const void *isr_inf);


struct timer_device
{
    struct device_node    *backing_dev;  /* backing timer device      */
    struct list_head active_q;      /* timer queue               */
    struct list_head pend_q;
    struct spinlock  lock_active_q;       /* lock to ptorect the queue */
    struct spinlock  lock_pend_q; 
    struct time_spec step;
    struct time_spec next_increment;
    struct time_spec current_increment;
};

struct timer
{
    struct list_node       node;
    timer_handler_t   callback;
    void              *arg;
    struct time_spec       to_sleep;
    struct time_spec       cursor;
    uint8_t           flags;
};

struct timer_api
{
    int (*enable)      (struct device_node *dev);
    int (*disable)     (struct device_node *dev);
    int (*reset)       (struct device_node *dev);
    int (*set_handler) (struct device_node *dev, timer_tick_handler_t th, void *arg);
    int (*get_handler) (struct device_node *dev, timer_tick_handler_t *th, void **arg);
    int (*set_timer)   (struct device_node *dev, struct time_spec *tm);
    int (*set_mode)    (struct device_node *dev, uint8_t mode); 
}; 

int timer_set_system_timer
(
    struct device_node *dev
);

int timer_system_init
(
    void
);

int timer_enqeue
(
    struct timer_device     *timer_dev,
    struct time_spec     *ts,
    timer_handler_t func,
    void            *arg,
    uint8_t         flags
);

int timer_enqeue_static
(
    struct timer_device     *timer_dev,
    struct time_spec     *ts,
    timer_handler_t func,
    void            *arg,
    uint8_t         flags,
    struct timer         *tm
);

int timer_dequeue
(
    struct timer_device *timer_dev,
    struct timer *tm
);

#endif