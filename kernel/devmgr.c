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
static dev_t       root_bus;

static int devmgr_add_dev_to_drv
(
    dev_t *dev, 
    const drv_t *drv
);

static int devmgr_dev_add_to_parent
(
    dev_t *dev,
    dev_t *parent
);

int devmgr_init(void)
{
    linked_list_init(&drv_list);
    spinlock_init(&drv_list_lock);
    memset(&root_bus, 0, sizeof(dev_t));
    devmgr_dev_name_set(&root_bus, "root_bus");
    devmgr_dev_type_set(&root_bus, DEVMGR_ROOT_BUS);
    return(0);
}

/*
 * devmgr_dev_create - create a device structure
 */

int devmgr_dev_create(dev_t **dev)
{
    if(dev == NULL)
        return(-1);
    
    if(*dev == NULL)
    {
        (*dev) = kmalloc(sizeof(dev_t));

        if((*dev) == NULL)
            return(-1);
        
        (*dev)->flags |= DEVMGR_DEV_ALLOCATED;
    }

    memset((*dev), 0, sizeof(dev_t));
    
    return(0);
}

int devmgr_dev_delete(dev_t *dev)
{
    
    if(dev != NULL)
    {
        memset(dev, 0, sizeof(dev_t));

        if(dev->flags & DEVMGR_DEV_ALLOCATED)
            kfree(dev);
    }

    return(0);
}

int devmgr_dev_add(dev_t *dev, dev_t *parent)
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

int devmgr_drv_add(drv_t *drv)
{
    int status = 0;

    if(drv == NULL)
        return(-1);

    /* check if the driver is already in the list */

    if(devmgr_drv_find(drv->drv_name) != NULL)
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

int devmgr_drv_remove(drv_t *drv)
{
    int status = 0;

    if(drv == NULL)
        return(-1);

    spinlock_lock_interrupt(&drv_list_lock);

    /* If the driver is not in the list, then bail out */
    if(linked_list_find_node(&drv_list, &drv->drv_node))
        status = -1;

    else
        linked_list_remove(&drv_list, &drv->drv_node);
    
    spinlock_unlock_interrupt(&drv_list_lock);

    return(status);
}

char *devmgr_dev_type_get(dev_t *dev)
{
    if(dev == NULL)
        return(NULL);

    return(dev->dev_type);
}

char *devmgr_drv_type_get(drv_t *drv)
{
    if(drv == NULL)
        return(NULL);
        
    return(drv->drv_type);
}

int devmgr_dev_type_set(dev_t *dev, char *type)
{
    if(dev == NULL)
        return(-1);

    dev->dev_type = type;
    return(0);
}

int devmgr_dev_type_match(dev_t *dev, char *type)
{
    if(dev == NULL || dev->dev_type == NULL || type == NULL)
        return(0);
    
    return(!strcmp(dev->dev_type, type));
}

static int devmgr_dev_add_to_parent
(
    dev_t *dev,
    dev_t *parent
)
{
    if(parent == NULL)
        parent = &root_bus;

    if(dev->parent != NULL)
        return(-1);

    if(!linked_list_find_node(&parent->children, &dev->dev_node))
        return(-1);
    
    linked_list_add_head(&parent->children, &dev->dev_node);

    dev->parent = parent;

    return(0);
}

/* devmgr_find_driver - find a driver by name */

drv_t *devmgr_drv_find(const char *name)
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
    
    if(drv->drv_init)
        status = drv->drv_init(drv);

    return(status);
}

/* devmgr_dev_probe - probe the device */

int devmgr_dev_probe(dev_t *dev)
{
    int status        = -1;
    list_node_t *node = NULL;
    drv_t       *drv  = NULL;

    spinlock_lock_interrupt(&drv_list_lock);

    node = linked_list_first(&drv_list);

    /* Find the right driver */
    while(node)
    {
        status = -1;
        drv = (drv_t*)node;

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

        kprintf("%s %d STS %d DEV %s DRV %s\n",__FUNCTION__,__LINE__,status,dev->dev_name, drv->drv_name);

        if(!status)
        {   
            dev->flags |= DEVMGR_DEV_PROBED;
            dev->drv = drv;
            break;
        }
        node = linked_list_next(node);
    }

    spinlock_unlock_interrupt(&drv_list_lock);

    return(status);
}

int devmgr_dev_init(dev_t *dev)
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

int devmgr_dev_uninit(dev_t *dev)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
        return(status);
    
    if(dev->drv->dev_init)
        status = dev->drv->dev_uninit(dev);
    
    return(status);
}

int devmgr_drv_data_set(drv_t *drv, const void *data)
{
    if(drv == NULL)
        return(-1);

    drv->driver_pv = data;

    return(0);
}

void *devmgr_drv_data_get(const drv_t *drv)
{
    if(drv == NULL)
        return(NULL);

    return(drv->driver_pv);
}

int devmgr_dev_data_set(dev_t *dev, void *data)
{
    if(dev == NULL)
        return(-1);

    dev->dev_data = data;

    return(0);
}

void *devmgr_dev_data_get(const dev_t *dev)
{
    if(dev == NULL)
        return(NULL);
    
    return(dev->dev_data);
}

dev_t *devmgr_parent_get(const dev_t *dev)
{
    if(dev == NULL)
        return(NULL);
    
    return(dev->parent);
}

char *devmgr_dev_name_get(const dev_t *dev)
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

int devmgr_dev_name_set(dev_t *dev, char *name)
{
    dev->dev_name = name;
    return(0);
}

int devmgr_dev_index_set(dev_t *dev, uint32_t index)
{
    dev->index = index;
    return(0);
}

uint32_t devmgr_dev_index_get(dev_t *dev)
{
    return(dev->index);
}

void *devmgr_dev_api_get(dev_t *dev)
{
    if(dev == NULL || dev->drv == NULL)
        return(NULL);

    return(dev->drv->drv_api);
}

dev_t *devmgr_dev_get_by_name(const char *name, const uint32_t index)
{
    dev_t *dev = NULL;
    dev_t *dev_stack[DEVMGR_SRCH_STACK];
    list_node_t *node        = NULL;
    int          stack_index = 0;

    memset(dev_stack, 0, sizeof(dev_stack));

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (dev_t*)node;

            if(linked_list_count(&dev->children) > 0)
            {
                if(stack_index < DEVMGR_SRCH_STACK)
                    dev_stack[stack_index++] = dev;
            }

            kprintf("DEVICE %s\n",dev->dev_name);

            if(dev->index == index && 
               !strcmp(dev->dev_name, name))
            {
                return(dev);
            }
            node = linked_list_next(node);
        }

        if(stack_index > 0)
        {
            stack_index--;
            node = linked_list_first(&dev_stack[stack_index]->children);
        }

        if(node == NULL)
            break;
    }

    return(NULL);
}

dev_srch_t *devmgr_dev_first(char *name, dev_t **dev_out)
{
    dev_t *dev = NULL;
    dev_srch_t *dev_srch = NULL;
    dev_t *dev_stack[DEVMGR_SRCH_STACK];
    list_node_t *node        = NULL;
    

    dev_srch = kcalloc(1, sizeof(dev_srch_t));
    dev_srch->stack = kcalloc(sizeof(dev_t*), DEVMGR_SRCH_STACK);

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (dev_t*)node;

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

dev_t *devmgr_dev_next(dev_srch_t *sh)
{
    dev_t *dev = NULL;
    dev_srch_t *dev_srch = sh;
    dev_t *dev_stack[DEVMGR_SRCH_STACK];
    list_node_t *node        = NULL;
    
    /* Continue from we left off */
    node = sh->next_node;

    for(;;)
    {
        while(node)
        {
            dev = (dev_t*)node;

            if(linked_list_count(&dev->children) > 0)
            {
                if(dev_srch->stack_index < DEVMGR_SRCH_STACK)
                    dev_srch->stack[dev_srch->stack_index++] = dev;
            }

            kprintf("DEVICE %s\n",dev->dev_name);

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

void *devmgr_drv_api_get(drv_t *drv)
{
    return(drv->drv_api);
}