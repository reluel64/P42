
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>
#include <platform.h>

static int timer_dev_loop_callback
(
    void *pv, 
    uint32_t interval, 
    isr_info_t *inf
)
{
    uint32_t *delay = (uint32_t*)pv;

    __atomic_add_fetch((int*)pv, interval, __ATOMIC_RELEASE);

    return(1);
}

int timer_dev_loop_delay
(
    device_t *dev, 
    uint32_t delay_ms
)
{
    uint32_t      cursor   = 0;
    timer_dev_cb  cb       = NULL;
    void         *data     = NULL;

    /* save the old cb */
    timer_dev_get_cb(dev, &cb, &data);

    /* set the new callback */
    timer_dev_connect_cb(dev, timer_dev_loop_callback, &cursor);
    
    /* reset the timer */
    timer_dev_reset(dev);

    while(__atomic_load_n(&cursor, __ATOMIC_ACQUIRE) < delay_ms)
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
    timer_dev_cb  *cb,
    void         **cb_pv
)
{
    timer_api_t *api = NULL;

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
    timer_dev_cb cb,
    void *cb_pv
)
{
    timer_api_t *api = NULL;

    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);
    
    api->install_cb(dev, cb, cb_pv);

    return(0);
}

int timer_dev_disconnect_cb
(
    device_t *dev,
    timer_dev_cb cb,
    void *cb_pv
)
{
    timer_api_t *api = NULL;

    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);
    
    api->uninstall_cb(dev, cb, cb_pv);

    return(0);
}

int timer_dev_disable(device_t *dev)
{
    timer_api_t *api = NULL;

    api = devmgr_dev_api_get(dev);

    if(api->disable)
        api->disable(dev);

    return(0);
}

int timer_dev_enable(device_t *dev)
{
    timer_api_t *api = NULL;

    api = devmgr_dev_api_get(dev);

    if(api->enable)
        api->enable(dev);

    return(0);
}

int timer_dev_reset(device_t *dev)
{
    timer_api_t *api = NULL;

    api = devmgr_dev_api_get(dev);

    if(api->reset)
        api->reset(dev);

    return(0);
}