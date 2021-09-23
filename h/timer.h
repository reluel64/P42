#ifndef timerh
#define timerh

#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#include <devmgr.h>
#include <isr.h>


#define TIMER_DEVICE_TYPE "timer"
#define TIMER_NOT_INIT    (1 << 0)


typedef uint32_t (*timer_dev_cb_t)(void *arg, void *isr_frame);

typedef struct timer_int_t
{
    uint64_t nanosec;
    uint32_t seconds;
}timer_int_t;

typedef struct timer_dev_t
{
    list_head_t timer_queue;
    spinlock_t queue_lock;
    device_t *timer_dev;
    uint32_t tm_step;
    uint64_t tm_ticks;
}timer_dev_t;

typedef struct timer_t
{
    list_node_t node;
    timer_dev_cb_t cb;
    void *arg;
    uint32_t delay;
    uint32_t cursor;
    uint8_t flags;
}timer_t;

typedef struct timer_api_t
{
    int (*install_cb)(device_t *dev, timer_dev_cb_t, void *);    
    int (*uninstall_cb)(device_t *dev, timer_dev_cb_t, void *);
    int (*get_cb)(device_t *dev, timer_dev_cb_t *, void **);
    int (*enable)(device_t *dev);
    int (*disable)(device_t *dev);
    int (*reset)  (device_t *dev);
    int (*tm_res_get) (device_t *dev, timer_int_t *tm);
}timer_api_t; 


int timer_dev_loop_delay
(
    device_t *dev, 
    uint32_t delay_ms
);

int timer_dev_get_cb
(
    device_t *dev,
    timer_dev_cb_t *cb,
    void **cb_pv
);

int timer_dev_connect_cb
(
    device_t *dev,
    timer_dev_cb_t cb,
    void *cb_pv
);

int timer_dev_disconnect_cb
(
    device_t *dev,
    timer_dev_cb_t cb,
    void *cb_pv
);

int timer_dev_disable(device_t *dev);
int timer_dev_enable(device_t *dev);
int timer_dev_reset(device_t *dev);
#endif