#include <linked_list.h>
#include <intc.h>
#include <spinlock.h>
#include <utils.h>


int intc_disable(device_t *dev)
{
    intc_api_t *api = NULL;
    
    if(!devmgr_dev_type_match(dev, INTERRUPT_CONTROLLER))
    {
        return(-1);
    }   
    api = devmgr_dev_api_get(dev);

    if(api == NULL || api->disable ==  NULL)
        return(-1);

    return(api->disable(dev));
}

int intc_enable(device_t *dev)
{
    intc_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, INTERRUPT_CONTROLLER))
        return(-1);

    api = devmgr_dev_api_get(dev);

    if(api == NULL || api->enable ==  NULL)
        return(-1);

    return(api->enable(dev));
}

int intc_send_ipi(device_t *dev, ipi_packet_t *ipi)
{
    intc_api_t *api = NULL;

    if(!devmgr_dev_type_match(dev, INTERRUPT_CONTROLLER))
        return(-1);
kprintf("MATCH\n");
    api = devmgr_dev_api_get(dev);

    if(api == NULL || api->send_ipi ==  NULL)
        return(-1);

    return(api->send_ipi(dev, ipi));
}