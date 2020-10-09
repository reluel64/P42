#include <linked_list.h>
#include <devmgr.h>
#include <spinlock.h>

static list_head_t drv_list;
static spinlock_t  drv_list_lock;


int devmgr_init(void)
{
    linked_list_init(&drv_list);
    spinlock_init(&drv_list_lock);
    return(0);
}

/*
 * devmgr_add_drv - add device driver
 */

int devmgr_add_drv(const drv_t *drv)
{
    int status = 0;

    if(drv == NULL)
        return(-1);

    /* check if the driver is already in the list */

    if(devmgr_find_driver(drv->drv_name) != NULL)
    {
        status = -1;
    }
    else
    {
        spinlock_lock_interrupt(&drv_list_lock);

        linked_list_add_tail(&drv_list, &drv->drv_node);

        spinlock_unlock_interrupt(&drv_list_lock);
    }

    return(status);
}

/*
 * devmgr_remove_drv - remove device driver
 */

int devmgr_remove_drv(const drv_t *drv)
{
    int status = 0;

    if(drv == NULL)
        return(-1);

    spinlock_lock_interrupt(&drv_list_lock);

    /* If the driver is not in the list, then bail out */
    if(linked_list_find_node(&drv_list, &drv->drv_node))
        status = -1;
  
    /* If we have devices, attached to the driver, 
     * we cannot remove it 
     */
    else if(linked_list_count(&drv->devs) > 0)
        status = -1;

    else
        linked_list_remove(&drv_list, &drv->drv_node);
    
    spinlock_unlock_interrupt(&drv_list_lock);

    return(status);
}

/* devmgr_add_dev_to_drv - add a device to the driver's list */

static int devmgr_add_dev_to_drv
(
    dev_t *dev, 
    const drv_t *drv
)
{
    int status = 0;

    spinlock_lock_interrupt(&drv_list_lock);

    if(linked_list_find_node(&drv->devs, &dev->dev_node))
        status = -1;
    else
        linked_list_add_tail(&drv->devs, &dev->dev_node);

    dev->drv = drv;
    
    spinlock_unlock_interrupt(&drv_list_lock);

    return(status);
}

/* devmgr_find_driver - find a driver by name */

drv_t *devmgr_find_driver(const char *name)
{
    drv_t       *drv  = NULL;
    list_node_t *node = NULL;

    spinlock_lock_interrupt(&drv_list_lock);
    node = linked_list_first(&drv_list);
    
    while(node)
    {
        drv = (drv_t*)node;

        if(!strcmp(drv->drv_name, name))
        {
            break;
        }
        
        drv = NULL;

        node = linked_list_next(node);
    }

    spinlock_unlock_interrupt(&drv_list_lock);

    return(drv);
}

/* devmgr_drv_init - initialize the driver */

int devmgr_drv_init(drv_t *drv)
{
    int status = -1;

    if(drv == NULL)
        return(status);

    linked_list_init(&drv->devs);
    spinlock_init(&drv->dev_list_lock);
    
    if(drv->drv_init)
        status = drv->drv_init(drv);

    return(status);
}

/* devmgr_dev_probe - probe the device */

int devmgr_dev_probe(dev_t *dev)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
        return(status);
    
    if(dev->drv->dev_probe)
        status = dev->drv->dev_probe(dev);
    
    return(status);
}

int devmgr_dev_init(dev_t *dev)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
        return(status);
    
    if(dev->drv->dev_init)
        status = dev->drv->dev_init(dev);
    
    return(status);
}

int devmgr_dev_uninit(dev_t *dev)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
        return(status);
    
    if(dev->drv->dev_init)
        status = dev->drv->dev_uninit(dev);
    
    return(status);
}

int devmgr_drv_set_data(drv_t *drv, const void *data)
{
    if(drv == NULL)
        return(-1);

    drv->driver_pv = data;

    return(0);
}

void *devmgr_drv_get_data(const drv_t *drv)
{
    if(drv == NULL)
        return(NULL);

    return(drv->driver_pv);
}

int devmgr_dev_set_data(dev_t *dev, void *data)
{
    if(dev == NULL)
        return(-1);

    dev->dev_data = data;

    return(0);
}

void *devmgr_dev_get_data(const dev_t *dev)
{
    if(dev == NULL)
        return(NULL);
    
    return(dev->dev_data);
}

dev_t *devmgr_get_parent(const dev_t *dev)
{
    if(dev == NULL)
        return(NULL);
    
    return(dev->parent);
}

char *devmgr_dev_get_name(const dev_t *dev)
{
    if(dev == NULL)
        return(NULL);

    return(dev->dev_name);
}

int devmgr_dev_name_match(const dev_t *dev, char *name)
{
    if(dev == NULL || dev->dev_name == NULL)
        return(0);
    
    if(strcmp(dev->dev_name, name) == 0)
        return(1);
    
    return(0);

}

dev_type_t devmgr_get_type(const dev_t *dev)
{
    if(dev == NULL)
        return(dev_type_error);
        
    return(dev->type);
}

