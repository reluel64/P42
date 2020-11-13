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
    int (*arm_timer)(device_t *, uint32_t delay);
    int (*disarm_timer)(device_t *);

}timer_api_t;

void *timer_arm
(
    device_t *dev, 
    timer_handler_t cb, 
    void *data,
    uint32_t delay
);
void timer_loop_delay(device_t *dev, uint32_t delay);

void timer_update(uint32_t interval);
#endif