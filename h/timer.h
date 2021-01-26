#ifndef timerh
#define timerh

#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#include <devmgr.h>
#include <isr.h>

#define TIMER_DEVICE_TYPE "timer"

typedef int (*timer_handler_t)(void *, isr_info_t *);
typedef int (*timer_dev_cb)(void *, uint32_t, isr_info_t *);

typedef struct timer_api_t
{
    int (*install_cb)(device_t *dev, timer_dev_cb, void *);    
    int (*uninstall_cb)(device_t *dev, timer_dev_cb, void *);
    int (*get_cb)(device_t *dev, timer_dev_cb *, void **);
    int (*tm_res)(device_t *dev, uint32_t *val, int *tp);
    int (*enable)(device_t *dev);
    int (*disable)(device_t *dev);
    int (*reset)  (device_t *dev);
}timer_api_t;


int timer_dev_loop_delay
(
    device_t *dev, 
    uint32_t delay_ms
);

int timer_dev_get_cb
(
    device_t *dev,
    timer_dev_cb *cb,
    void **cb_pv
);

int timer_dev_connect_cb
(
    device_t *dev,
    timer_dev_cb cb,
    void *cb_pv
);

int timer_dev_disconnect_cb
(
    device_t *dev,
    timer_dev_cb cb,
    void *cb_pv
);

int timer_dev_disable(device_t *dev);
int timer_dev_enable(device_t *dev);
int timer_dev_reset(device_t *dev);
#endif