/* Device manager
 * Part of P42
 */ 
#include <linked_list.h>
#include <devmgr.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#define DEVMGR_SRCH_STACK 128


static list_head_t drv_list;
static spinlock_t  drv_list_lock;
static device_t     root_bus;
static spinlock_t  dev_list_lock;

static int devmgr_add_device_to_drv
(
    device_t *dev, 
    const driver_t *drv
);

static int devmgr_dev_add_to_parent
(
    device_t *dev,
    device_t *parent
);

int devmgr_show_devices(void)
{
    device_t *dev = NULL;
    list_node_t **dev_stack = NULL;
    list_node_t *node        = NULL;
    int          stack_index = 0;

    dev_stack = kcalloc(DEVMGR_SRCH_STACK, sizeof(list_node_t*));

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (device_t*)node;
#if 0
            if(linked_list_count(&dev->children) > 0)
            {
                if(stack_index < DEVMGR_SRCH_STACK)
                    dev_stack[stack_index++] = dev;
            }
#endif


            kprintf("DEVICE %s TYPE %s Index %d PARENT %s\n",
                    dev->dev_name,
                    dev->dev_type, 
                    dev->index,
                    dev->parent->dev_name);

            /* start going down if we have children*/
            if(linked_list_count(&dev->children) > 0)
            {
                /*save the  parent node */
                if(stack_index < DEVMGR_SRCH_STACK)
                {
                    dev_stack[stack_index++] = node;
                    node = linked_list_first(&dev->children);
                    continue;
                }
            }

            node = linked_list_next(node);
        }

        if(stack_index > 0)
        {
            stack_index--;
            node = linked_list_next(dev_stack[stack_index]);
        }

        if(node == NULL && stack_index == 0)
            break;
    }
    kfree(dev_stack);
    return(0);
}

int devmgr_init(void)
{
    linked_list_init(&drv_list);
    spinlock_rw_init(&drv_list_lock);
    spinlock_rw_init(&dev_list_lock);
    memset(&root_bus, 0, sizeof(device_t));
    devmgr_dev_name_set(&root_bus, "root_bus");
    devmgr_dev_type_set(&root_bus, DEVMGR_ROOT_BUS);
    return(0);
}

/*
 * devmgr_dev_create - create a device structure
 */

int devmgr_dev_create(device_t **dev)
{
    if(dev == NULL)
        return(-1);
    
    if(*dev == NULL)
    {
        (*dev) = kcalloc(sizeof(device_t), 1);

        if((*dev) == NULL)
            return(-1);
        
        (*dev)->flags |= DEVMGR_DEV_ALLOCATED;
    }

    memset((*dev), 0, sizeof(device_t));
    
    return(0);
}

int devmgr_dev_delete(device_t *dev)
{
    
    if(dev != NULL)
    {
        memset(dev, 0, sizeof(device_t));

        if(dev->flags & DEVMGR_DEV_ALLOCATED)
            kfree(dev);
    }

    return(0);
}

int devmgr_dev_add(device_t *dev, device_t *parent)
{
    int          status = 0;

    if(dev == NULL || 
      (dev->flags & DEVMGR_DEV_INITIALIZED))
      {
        kprintf("WHAT????\n");
        kprintf("%s %d\n",__FUNCTION__,__LINE__);
        return(-1);
      }

    status = devmgr_dev_probe(dev);
    
    devmgr_dev_add_to_parent(dev, parent);
 
    kprintf("PROBE_STATUS %d\n",status);
    /* If we found the driver, then initialize the device */
    if(!status)
    {
        status = devmgr_dev_init(dev);
        kprintf("INIT_STATUS %d\n",status);
    }

    return(status);
}

/*
 * devmgr_add_drv - add device driver
 */

int devmgr_drv_add(driver_t *drv)
{
    int status = 0;
    
    uint8_t int_flag = 0;

    if(drv == NULL)
        return(-1);

    /* check if the driver is already in the list */

    if(devmgr_drv_find(drv->drv_name) != NULL)
    {
        status = -1;
    }
    else
    {
        spinlock_write_lock_int(&drv_list_lock, &int_flag);

        linked_list_add_tail(&drv_list, &drv->drv_node);

        spinlock_write_unlock_int(&drv_list_lock, int_flag);
    }

    return(status);
}

/*
 * devmgr_remove_drv - remove device driver
 */

int devmgr_drv_remove(driver_t *drv)
{
    int status = 0;
    uint8_t int_flag = 0;

    if(drv == NULL)
        return(-1);

    spinlock_write_lock_int(&drv_list_lock, &int_flag);

    /* If the driver is not in the list, then bail out */
    if(linked_list_find_node(&drv_list, &drv->drv_node))
        status = -1;

    else
        linked_list_remove(&drv_list, &drv->drv_node);
    
    spinlock_write_unlock_int(&drv_list_lock, int_flag);

    return(status);
}

char *devmgr_dev_type_get(device_t *dev)
{
    if(dev == NULL)
        return(NULL);

    return(dev->dev_type);
}

char *devmgr_drv_type_get(driver_t *drv)
{
    if(drv == NULL)
        return(NULL);
        
    return(drv->drv_type);
}

int devmgr_dev_type_set(device_t *dev, char *type)
{
    if(dev == NULL)
        return(-1);

    dev->dev_type = type;
    return(0);
}

int devmgr_dev_type_match(device_t *dev, char *type)
{
    if(dev == NULL || dev->dev_type == NULL || type == NULL)
        return(0);

    return(!strcmp(dev->dev_type, type));
}

static int devmgr_dev_add_to_parent
(
    device_t *dev,
    device_t *parent
)
{
    uint8_t int_flag = 0;

    if(parent == NULL)
        parent = &root_bus;

    if(dev->parent != NULL)
        return(-1);
   
    spinlock_write_lock_int(&dev_list_lock, &int_flag);
   
    if(!linked_list_find_node(&parent->children, &dev->dev_node))
    {
        spinlock_write_unlock_int(&dev_list_lock, int_flag);
        return(-1);
    }
    linked_list_add_tail(&parent->children, &dev->dev_node);
    
    dev->parent = parent;

    spinlock_write_unlock_int(&dev_list_lock, int_flag);
    
    return(0);
}

/* devmgr_find_driver - find a driver by name */

driver_t *devmgr_drv_find(const char *name)
{
    driver_t    *drv  = NULL;
    list_node_t *node = NULL;
    uint8_t     int_flag = 0;

    spinlock_read_lock_int(&drv_list_lock, &int_flag);
    
    node = linked_list_first(&drv_list);
    
    while(node)
    {
        drv = (driver_t*)node;

        if(!strcmp(drv->drv_name, name))
        {
            break;
        }
        
        drv = NULL;

        node = linked_list_next(node);
    }

    spinlock_read_unlock_int(&drv_list_lock, int_flag);

    return(drv);
}

/* devmgr_drv_init - initialize the driver */

int devmgr_drv_init(driver_t *drv)
{
    int status = -1;

    if(drv == NULL)
        return(status);

    if(drv->drv_init)
        status = drv->drv_init(drv);

    return(status);
}

/* devmgr_dev_probe - probe the device */

int devmgr_dev_probe(device_t *dev)
{
    int status        = -1;
    list_node_t *node = NULL;
    driver_t       *drv  = NULL;
    uint8_t int_flag = 0;

    spinlock_read_lock_int(&drv_list_lock, &int_flag);

    node = linked_list_first(&drv_list);

    /* Find the right driver */
    while(node)
    {
        status = -1;
        drv = (driver_t*)node;

        if(dev->dev_type != NULL && drv->drv_type != NULL)
        {
            if(strcmp(dev->dev_type, drv->drv_type))
            {
                node = linked_list_next(node);
                continue;
            }
        }

        if(drv->dev_probe)
            status = drv->dev_probe(dev);
        
        kprintf("%s %d STS %d DEV %s DRV %s\n",
                __FUNCTION__,
                __LINE__,
                status,
                dev->dev_name, 
                drv->drv_name);
        
        if(!status)
        {   
            dev->flags |= DEVMGR_DEV_PROBED;
            dev->drv = drv;
            break;
        }
        
        node = linked_list_next(node);
    }

    spinlock_read_unlock_int(&drv_list_lock, int_flag);

    return(status);
}

int devmgr_dev_init(device_t *dev)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
        return(status);
    
    if(!(dev->flags & DEVMGR_DEV_PROBED))
        return(status);

    if(dev->drv->dev_init)
        status = dev->drv->dev_init(dev);

    if(status == 0)
    {
        dev->flags |=  DEVMGR_DEV_INITIALIZED;
        dev->flags &= ~DEVMGR_DEV_PROBED;
    }
    
    return(status);
}

int devmgr_dev_uninit(device_t *dev)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
        return(status);
    
    if(dev->drv->dev_init)
        status = dev->drv->dev_uninit(dev);
    
    return(status);
}

int devmgr_drv_data_set(driver_t *drv, void *data)
{
    if(drv == NULL)
        return(-1);

    drv->driver_pv = data;

    return(0);
}

void *devmgr_drv_data_get(const driver_t *drv)
{
    if(drv == NULL)
        return(NULL);

    return(drv->driver_pv);
}

int devmgr_dev_data_set(device_t *dev, void *data)
{
    if(dev == NULL)
        return(-1);

    dev->dev_data = data;

    return(0);
}

void *devmgr_dev_data_get(const device_t *dev)
{
    if(dev == NULL)
        return(NULL);
    
    return(dev->dev_data);
}

device_t *devmgr_dev_parent_get(const device_t *dev)
{
    if(dev == NULL)
        return(NULL);
    
    return(dev->parent);
}

char *devmgr_dev_name_get(const device_t *dev)
{
    if(dev == NULL)
        return(NULL);

    return(dev->dev_name);
}

int devmgr_dev_name_match(const device_t *dev, char *name)
{
    if(dev == NULL || dev->dev_name == NULL)
        return(0);
    
    if(strcmp(dev->dev_name, name) == 0)
        return(1);
    
    return(0);
}

int devmgr_dev_name_set(device_t *dev, char *name)
{
    dev->dev_name = name;
    return(0);
}

int devmgr_dev_index_set(device_t *dev, uint32_t index)
{
    dev->index = index;
    return(0);
}

uint32_t devmgr_dev_index_get(device_t *dev)
{
    return(dev->index);
}

void *devmgr_dev_api_get(device_t *dev)
{
    if(dev == NULL || dev->drv == NULL)
        return(NULL);

    return(dev->drv->drv_api);
}

device_t *devmgr_dev_get_by_name
(
    const char     *name, 
    const uint32_t index
)
{
    device_t *dev = NULL;
    list_node_t *dev_stack[DEVMGR_SRCH_STACK];
    list_node_t *node        = NULL;
    int          stack_index = 0;
    uint8_t      int_status = 0;

    memset(dev_stack, 0, sizeof(dev_stack));
    
    spinlock_read_lock_int(&dev_list_lock, &int_status);

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (device_t*)node;


            if(dev->index == index && 
               !strcmp(dev->dev_name, name))
            {
                spinlock_read_unlock_int(&dev_list_lock, int_status);
                return(dev);
            }

            /* start going down if we have children*/
            if(linked_list_count(&dev->children) > 0)
            {
                /*save the  parent node */
                if(stack_index < DEVMGR_SRCH_STACK)
                {
                    dev_stack[stack_index++] = node;
                    node = linked_list_first(&dev->children);
                    continue;
                }
            }
            
            node = linked_list_next(node);
        }

        if(stack_index > 0)
        {
            stack_index--;
            node = linked_list_next(dev_stack[stack_index]);
        }

        if(node == NULL && stack_index == 0)
            break;
    }

    spinlock_read_unlock_int(&dev_list_lock, int_status);

    return(NULL);
}

void *devmgr_drv_api_get(driver_t *drv)
{
    if(drv == NULL)
        return(NULL);

    return(drv->drv_api);
}

driver_t *devmgr_dev_drv_get(device_t *dev)
{
    if(dev == NULL)
        return(NULL);

    return(dev->drv);
}

/* The dev_first/dev_next/dev_end should be implemented
 * more nicely but since there is not need for such
 * API for now, we will leave them as primitive implementations
 */ 
#if 0
dev_srch_t *devmgr_dev_first(char *name, device_t **dev_out)
{
    device_t       *dev      = NULL;
    dev_srch_t  *dev_srch = NULL;
    list_node_t *node     = NULL;
    

    dev_srch = kcalloc(1, sizeof(dev_srch_t));
    dev_srch->stack = kcalloc(sizeof(device_t*), DEVMGR_SRCH_STACK);

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (device_t*)node;

            if(linked_list_count(&dev->children) > 0)
            {
                if(dev_srch->stack_index < DEVMGR_SRCH_STACK)
                    dev_srch->stack[dev_srch->stack_index++] = dev;
            }

            kprintf("DEVICE %s\n",dev->dev_name);

            if(strcmp(dev->dev_name, name))
            {
                dev_srch->dev_name = name;
                dev_srch->this_node = node;
                *dev_out = dev;
                return(dev_srch);
            }

            node = linked_list_next(node);
        }

        if(dev_srch->stack_index > 0)
        {
            dev_srch->stack_index--;
            node = linked_list_first(&dev_srch->stack[dev_srch->stack_index]->children);
        }

        if(node == NULL)
            break;
    }

    return(NULL);
}

device_t *devmgr_dev_next(dev_srch_t *sh)
{
    device_t       *dev      = NULL;
    dev_srch_t  *dev_srch = sh;
    list_node_t *node     = NULL;
    
    /* Continue from we left off */
    node = sh->next_node;

    for(;;)
    {
        while(node)
        {
            dev = (device_t*)node;

            if(linked_list_count(&dev->children) > 0)
            {
                if(dev_srch->stack_index < DEVMGR_SRCH_STACK)
                    dev_srch->stack[dev_srch->stack_index++] = dev;
            }

           // kprintf("DEVICE %s\",dev->dev_name);

            if(strcmp(dev->dev_name, dev_srch->dev_name))
            {
                dev_srch->this_node = node;
                dev_srch->next_node = linked_list_next(node);
                return(dev);
            }

            node = linked_list_next(node);
        }

        if(dev_srch->stack_index > 0)
        {
            dev_srch->stack_index--;
            node = linked_list_first(&dev_srch->stack[dev_srch->stack_index]->children);
        }

        if(node == NULL)
            break;
    }

    return(NULL);
}

int devmgr_dev_end(dev_srch_t *sh)
{
    if(sh != NULL)
    {
        kfree(sh->stack);
        kfree(sh);
        return(0);
    }

    return(-1);
}
#endif
