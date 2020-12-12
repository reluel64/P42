#ifndef timerh
#define timerh

#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#include <devmgr.h>

#define TIMER_DEVICE_TYPE "timer"
#define TIMER_PERIODIC    (1 << 0)



typedef int (*timer_handler_t)(void *, virt_addr_t);

typedef struct timer_t
{
    list_node_t node;
    uint32_t ctime;
    uint32_t ttime;
    timer_handler_t handler;
    void *data;
    uint32_t flags;
}timer_t;

typedef struct timer_api_t
{
    int (*arm_timer)(device_t *, timer_t *);
    int (*disarm_timer)(device_t *, timer_t *);

}timer_api_t;

void *timer_arm
(
    device_t *dev, 
    timer_handler_t cb, 
    void *data,
    uint32_t delay
);
void timer_loop_delay(device_t *dev, uint32_t delay);
int timer_periodic_install
(
    device_t *dev,
    timer_handler_t cb, 
    void *pv, 
    uint32_t period
);

void timer_update
(
    list_head_t *queue,
    uint32_t interval,
    virt_addr_t iframe
);
#endif