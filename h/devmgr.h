#ifndef deviceh
#define deviceh

#include <linked_list.h>

typedef struct drv_t drv_t;
typedef struct dev_t dev_t;

typedef enum dev_type_t
{
    dev_type_error = 0x0,
    dev_type_bus,
    dev_type_intc
}dev_type_t;

/* Driver structure */
typedef struct drv_t
{
    list_node_t drv_node;
    char *drv_name; 
    int (*dev_probe)  (dev_t *);
    int (*dev_init)   (dev_t *);
    int (*dev_uninit) (dev_t *);
    int (*drv_init)   (drv_t *);
    int (*drv_uninit) (drv_t *);
    void *drv_api;
    spinlock_t dev_list_lock;
    void *driver_pv;
    list_head_t devs;
}drv_t;

/* Device structure */
typedef struct dev_t
{
    list_node_t dev_node;
    char *dev_name;
    dev_type_t type;
    drv_t *drv;
    dev_t *parent;
    void *dev_data;
    list_head_t children;
}dev_t;


int devmgr_init(void);
int devmgr_add_drv(const drv_t *drv);
int devmgr_remove_drv(const drv_t *drv);
static int devmgr_add_dev_to_drv
(
    dev_t *dev, 
    const drv_t *drv
);
drv_t *devmgr_find_driver(const char *name);
int devmgr_drv_init(drv_t *drv);
int devmgr_dev_probe(dev_t *dev);
int devmgr_dev_init(dev_t *dev);
int devmgr_drv_set_data(drv_t *drv, const void *data);
void *devmgr_drv_get_data(const drv_t *drv);
int devmgr_dev_set_data(dev_t *dev, void *data);
void *devmgr_dev_get_data(const dev_t *dev);
dev_t *devmgr_get_parent(const dev_t *dev);
dev_type_t devmgr_get_type(const dev_t *dev);
char *devmgr_dev_get_name(const dev_t *dev);
int devmgr_dev_name_match(const dev_t *dev, char *name);
#endif