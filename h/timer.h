#ifndef timerh
#define timerh

#include <stdint.h>
#include <linked_list.h>
#include <devmgr.h>

#define TIMER_DEVICE_TYPE "timer"

typedef int (*timer_handler_t)(void *);

typedef struct timer_t
{
    list_node_t node;
    uint32_t ctime;
    uint32_t ttime;
    timer_handler_t handler;
    void *data;
}timer_t;

typedef struct timer_api_t
{
    int (*arm_timer)(dev_t *, uint32_t delay);
    int (*disarm_timer)(dev_t *);

}timer_api_t;

void *timer_arm
(
    dev_t *dev, 
    timer_handler_t *cb, 
    void *data,
    uint32_t delay
);
void timer_loop_delay(dev_t *dev, uint32_t delay);

void timer_update(uint32_t interval);
#endif