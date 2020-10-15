#ifndef deviceh
#define deviceh

#include <linked_list.h>
#include <spinlock.h>
#define DEVMGR_DEV_INITIALIZED (1 << 0)
#define DEVMGR_DEV_PROBED      (1 << 1)
#define DEVMGR_DEV_ALLOCATED   (1 << 2)


#define DEVMGR_ROOT_BUS "root_bus_type"


typedef struct drv_t drv_t;
typedef struct dev_t dev_t;

/* Driver structure */
typedef struct drv_t
{
    list_node_t drv_node;
    char *drv_name;
    char *drv_type;
    int (*dev_probe)  (dev_t *);
    int (*dev_init)   (dev_t *);
    int (*dev_uninit) (dev_t *);
    int (*drv_init)   (drv_t *);
    int (*drv_uninit) (drv_t *);
    void *drv_api;
    void *driver_pv;
}drv_t;

/* Device structure */
typedef struct dev_t
{
    list_node_t  dev_node;
    char        *dev_name;
    char        *dev_type;
    drv_t       *drv;
    dev_t       *parent;
    void        *dev_data;
    uint32_t     flags;
    uint32_t     index;
    list_head_t  children;
}dev_t;


int devmgr_init(void);
int devmgr_add_drv(drv_t *drv);
int devmgr_remove_drv(drv_t *drv);
static int devmgr_add_dev_to_drv
(
    dev_t *dev, 
    const drv_t *drv
);

int devmgr_init(void);
int devmgr_dev_create(dev_t **dev);
int devmgr_dev_add(dev_t *dev, dev_t *parent);
int devmgr_drv_add(drv_t *drv);
int devmgr_drv_remove(drv_t *drv);
char *devmgr_dev_type_get(dev_t *dev);
char *devmgr_drv_type_get(drv_t *drv);
int devmgr_dev_type_set(dev_t *dev, char *type);
int devmgr_dev_type_match(dev_t *dev, char *type);
drv_t *devmgr_drv_find(const char *name);
int devmgr_drv_init(drv_t *drv);
int devmgr_dev_probe(dev_t *dev);
int devmgr_dev_init(dev_t *dev);
int devmgr_dev_uninit(dev_t *dev);
int devmgr_drv_data_set(drv_t *drv, const void *data);
void *devmgr_drv_data_get(const drv_t *drv);
int devmgr_dev_data_set(dev_t *dev, void *data);
void *devmgr_dev_data_get(const dev_t *dev);
dev_t *devmgr_parent_get(const dev_t *dev);
char *devmgr_dev_name_get(const dev_t *dev);
int devmgr_dev_name_match(const dev_t *dev, char *name);
int devmgr_dev_name_set(dev_t *dev, char *name);
int devmgr_dev_index_set(dev_t *dev, uint32_t index);
uint32_t devmgr_dev_index_get(dev_t *dev);
void *devmgr_dev_api_get(dev_t *dev);
dev_t *devmgr_dev_get_by_name(const char *name, const uint32_t index);
#endif