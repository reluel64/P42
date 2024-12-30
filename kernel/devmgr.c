/* Device manager
 * Part of P42
 */ 
#include <linked_list.h>
#include <devmgr.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#define DEVMGR_SRCH_STACK 128


static struct list_head drv_list;
static struct spinlock_rw  drv_list_lock;
static struct device_node    root_bus;
static struct spinlock_rw  dev_list_lock;

static int devmgr_dev_add_to_parent
(
    struct device_node *dev,
    struct device_node *parent
);

int devmgr_show_devices
(
    void
)
{
    struct device_node *dev = NULL;
    struct list_node **dev_stack = NULL;
    struct list_node *node        = NULL;
    int          stack_index = 0;

    dev_stack = kcalloc(DEVMGR_SRCH_STACK, sizeof(struct list_node*));

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (struct device_node*)node;
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
        {
            break;
        }
    }
    kfree(dev_stack);
    return(0);
}

int devmgr_init
(
    void
)
{
    linked_list_init(&drv_list);
    spinlock_rw_init(&drv_list_lock);
    spinlock_rw_init(&dev_list_lock);
    memset(&root_bus, 0, sizeof(struct device_node));
    devmgr_dev_name_set(&root_bus, "root_bus");
    devmgr_dev_type_set(&root_bus, DEVMGR_ROOT_BUS);
    return(0);
}

/*
 * devmgr_dev_create - create a device structure
 */

int devmgr_dev_create
(
    struct device_node **dev
)
{
    if(dev == NULL)
    {
        return(-1);
    }

    if(*dev == NULL)
    {
        (*dev) = kcalloc(sizeof(struct device_node), 1);

        if((*dev) == NULL)
        {
            return(-1);
        }
        
        (*dev)->flags |= DEVMGR_DEV_ALLOCATED;
    }
    else
    {
        memset((*dev), 0, sizeof(struct device_node));
    }

    return(0);
}

int devmgr_dev_delete
(
    struct device_node *dev
)
{
    uint32_t flags = 0;

    if(dev != NULL && 
       (linked_list_count(&dev->children) == 0))
    {

        flags = dev->flags;
        memset(dev, 0, sizeof(struct device_node));

        if(flags & DEVMGR_DEV_ALLOCATED)
        {
            kfree(dev);
        }
    }

    return(0);
}

int devmgr_dev_remove
(
    struct device_node *dev, 
    uint8_t remove_children
)
{
    struct device_node *child       = NULL;
    uint8_t   int_flag    = 0;
    size_t    stack_index = 0;
    struct list_node *dev_stack[DEVMGR_SRCH_STACK];
    struct list_node *node = NULL;
    struct list_node   *next_node = NULL;

    if(dev == NULL)
    {
        return(-1);
    }

    /* check if we are trying to remove the root bus - which would be stupid */
    if(dev->parent == NULL)
    {
        return(-1);
    }

    spinlock_write_lock_int(&dev_list_lock, &int_flag);

    if((linked_list_count(&dev->children) > 0) && (remove_children == 0))
    {
        spinlock_write_unlock_int(&dev_list_lock, int_flag);
        return(-1);
    }

    memset(dev_stack, 0, sizeof(dev_stack));

    node = linked_list_first(&dev->children);
  
    while(linked_list_count(&dev->children) > 0)
    {
        while(node)
        {
            child = (struct device_node*)node;
            next_node = linked_list_next(node);

            /* start going down if we have children*/
            if(linked_list_count(&child->children) > 0)
            {
                /*save the  parent node */
                if(stack_index < DEVMGR_SRCH_STACK)
                {
                    dev_stack[stack_index++] = node;
                    node = linked_list_first(&child->children);
                    continue;
                }
            }
            else
            {
                linked_list_remove(&child->parent->children, &child->dev_node);
                devmgr_dev_uninit(child);
                devmgr_dev_delete(child);
                child = NULL;
            }
            
            node = next_node;
        }

        if(stack_index > 0)
        {
            stack_index--;
            node = dev_stack[stack_index];
        }

        if(node == NULL && stack_index == 0)
        {
            break;
        }
    }

    /* check again if we do not have any children */
    if(linked_list_count(&dev->children) == 0)
    {
        linked_list_remove(&dev->parent->children, &dev->dev_node);
        devmgr_dev_uninit(dev);
        devmgr_dev_delete(dev);
    }

    spinlock_write_unlock_int(&dev_list_lock, int_flag);

    return(0);
}

int devmgr_dev_add
(
    struct device_node *dev, 
    struct device_node *parent
)
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
 
    /* If we found the driver, then initialize the device */
    if(!status)
    {
        status = devmgr_dev_init(dev);
    }

    return(status);
}

/*
 * devmgr_add_drv - add device driver
 */

int devmgr_drv_add
(
    struct driver_node *drv
)
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

int devmgr_drv_remove
(
    struct driver_node *drv
)
{
    int status = 0;
    uint8_t int_flag = 0;

    if(drv == NULL)
    {
        return(-1);
    }

    spinlock_write_lock_int(&drv_list_lock, &int_flag);

    /* If the driver is not in the list, then bail out */
    if(linked_list_find_node(&drv_list, &drv->drv_node))
    {
        status = -1;
    }
    else
    {
        linked_list_remove(&drv_list, &drv->drv_node);
    }
    
    spinlock_write_unlock_int(&drv_list_lock, int_flag);

    return(status);
}

char *devmgr_dev_type_get
(
    struct device_node *dev
)
{
    if(dev == NULL)
        return(NULL);

    return(dev->dev_type);
}

char *devmgr_drv_type_get
(
    struct driver_node *drv
)
{
    if(drv == NULL)
        return(NULL);
        
    return(drv->drv_type);
}

int devmgr_dev_type_set
(
    struct device_node *dev, 
    char *type
)
{
    if(dev == NULL)
    {
        return(-1);
    }

    dev->dev_type = type;
    return(0);
}

int devmgr_dev_type_match
(
    struct device_node *dev, 
    char *type
)
{
    if(dev == NULL || dev->dev_type == NULL || type == NULL)
    {
        return(0);
    }

    return(!strcmp(dev->dev_type, type));
}

static int devmgr_dev_add_to_parent
(
    struct device_node *dev,
    struct device_node *parent
)
{
    uint8_t int_flag = 0;

    if(parent == NULL)
    {
        parent = &root_bus;
    }

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

struct driver_node *devmgr_drv_find
(
    const char *name
)
{
    struct driver_node    *drv  = NULL;
    struct list_node *node = NULL;
    uint8_t     int_flag = 0;

    spinlock_read_lock_int(&drv_list_lock, &int_flag);
    
    node = linked_list_first(&drv_list);
    
    while(node)
    {
        drv = (struct driver_node*)node;

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

int devmgr_drv_init
(
    struct driver_node *drv
)
{
    int status = -1;

    if(drv == NULL)
    {
        return(status);
    }

    if(drv->drv_init)
    {
        status = drv->drv_init(drv);
    }

    return(status);
}

/* devmgr_dev_probe - probe the device */

int devmgr_dev_probe
(
    struct device_node *dev
)
{
    int status        = -1;
    struct list_node *node = NULL;
    struct driver_node       *drv  = NULL;
    uint8_t int_flag = 0;

    spinlock_read_lock_int(&drv_list_lock, &int_flag);

    node = linked_list_first(&drv_list);

    /* Find the right driver */
    while(node)
    {
        status = -1;
        drv = (struct driver_node*)node;

        if(dev->dev_type != NULL && drv->drv_type != NULL)
        {
            if(strcmp(dev->dev_type, drv->drv_type))
            {
                node = linked_list_next(node);
                continue;
            }
        }

        if(drv->dev_probe)
        {
            status = drv->dev_probe(dev);
        }

        #if 0
        kprintf("%s %d STS %d DEV %s DRV %s\n",
                __FUNCTION__,
                __LINE__,
                status,
                dev->dev_name, 
                drv->drv_name);
        #endif
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

int devmgr_dev_init
(
    struct device_node *dev
)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
    {
        return(status);
    }

    if(!(dev->flags & DEVMGR_DEV_PROBED))
    {
        return(status);
    }

    if(dev->drv->dev_init)
    {
        status = dev->drv->dev_init(dev);
    }

    if(status == 0)
    {
        dev->flags |=  DEVMGR_DEV_INITIALIZED;
        dev->flags &= ~DEVMGR_DEV_PROBED;
    }
    
    return(status);
}

int devmgr_dev_uninit
(
    struct device_node *dev
)
{
    int status = -1;

    if(dev == NULL || dev->drv == NULL)
    {
        return(status);
    }

    if(dev->drv->dev_init)
    {
        status = dev->drv->dev_uninit(dev);
    }

    dev->flags &= ~DEVMGR_DEV_INITIALIZED;
    
    return(status);
}

int devmgr_drv_data_set
(
    struct driver_node *drv, 
    void *data
)
{
    if(drv == NULL)
    {
        return(-1);
    }

    drv->driver_pv = data;

    return(0);
}

void *devmgr_drv_data_get
(
    const struct driver_node *drv
)
{
    if(drv == NULL)
    {
        return(NULL);
    }

    return(drv->driver_pv);
}

int devmgr_dev_data_set
(
    struct device_node *dev, 
    void *data
)
{
    if(dev == NULL)
    {
        return(-1);
    }

    dev->dev_data = data;

    return(0);
}

void *devmgr_dev_data_get
(
    const struct device_node *dev
)
{
    if(dev == NULL)
    {
        return(NULL);
    }

    return(dev->dev_data);
}

struct device_node *devmgr_dev_parent_get
(
    const struct device_node *dev
)
{
    if(dev == NULL)
    {
        return(NULL);
    }

    return(dev->parent);
}

char *devmgr_dev_name_get
(
    const struct device_node *dev
)
{
    if(dev == NULL)
    {
        return(NULL);
    }

    return(dev->dev_name);
}

int devmgr_dev_name_match
(
    const struct device_node *dev, 
    char *name
)
{
    if(dev == NULL || dev->dev_name == NULL)
    {
        return(0);
    }

    if(strcmp(dev->dev_name, name) == 0)
    {
        return(1);
    }

    return(0);
}

int devmgr_dev_name_set
(
    struct device_node *dev, 
    char *name
)
{
    dev->dev_name = name;
    return(0);
}

int devmgr_dev_index_set
(
    struct device_node *dev, 
    uint32_t index
)
{
    dev->index = index;
    return(0);
}

uint32_t devmgr_dev_index_get
(
    struct device_node *dev
)
{
    return(dev->index);
}

void *devmgr_dev_api_get
(
    struct device_node *dev
)
{
    if(dev == NULL || dev->drv == NULL)
    {
        return(NULL);
    }

    return(dev->drv->drv_api);
}

struct device_node *devmgr_dev_get_by_name
(
    const char     *name, 
    const uint32_t index
)
{
    struct device_node *dev = NULL;
    struct list_node *dev_stack[DEVMGR_SRCH_STACK];
    struct list_node *node        = NULL;
    int          stack_index = 0;
    uint8_t      int_status = 0;

    memset(dev_stack, 0, sizeof(dev_stack));
    
    spinlock_read_lock_int(&dev_list_lock, &int_status);

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (struct device_node*)node;


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

void *devmgr_drv_api_get
(
    struct driver_node *drv
)
{
    if(drv == NULL)
    {
        return(NULL);
    }

    return(drv->drv_api);
}

struct driver_node *devmgr_dev_drv_get
(
    struct device_node *dev
)
{
    if(dev == NULL)
    {
        return(NULL);
    }

    return(dev->drv);
}

/* The dev_first/dev_next/dev_end should be implemented
 * more nicely but since there is not need for such
 * API for now, we will leave them as primitive implementations
 */ 
#if 0
dev_srch_t *devmgr_dev_first(char *name, struct device_node **dev_out)
{
    struct device_node       *dev      = NULL;
    dev_srch_t  *dev_srch = NULL;
    struct list_node *node     = NULL;
    

    dev_srch = kcalloc(1, sizeof(dev_srch_t));
    dev_srch->stack = kcalloc(sizeof(struct device_node*), DEVMGR_SRCH_STACK);

    node = linked_list_first(&root_bus.children);

    for(;;)
    {
        while(node)
        {
            dev = (struct device_node*)node;

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

struct device_node *devmgr_dev_next(dev_srch_t *sh)
{
    struct device_node       *dev      = NULL;
    dev_srch_t  *dev_srch = sh;
    struct list_node *node     = NULL;
    
    /* Continue from we left off */
    node = sh->next_node;

    for(;;)
    {
        while(node)
        {
            dev = (struct device_node*)node;

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
