#ifndef deviceh
#define deviceh

#include <linked_list.h>
#include <spinlock.h>
#define DEVMGR_DEV_INITIALIZED (1 << 0)
#define DEVMGR_DEV_PROBED      (1 << 1)
#define DEVMGR_DEV_ALLOCATED   (1 << 2)


#define DEVMGR_ROOT_BUS "root_bus_type"


typedef struct driver_t driver_t;
typedef struct device_t device_t;

/* Driver structure */
typedef struct driver_t
{
    list_node_t drv_node;
    char *drv_name;
    char *drv_type;
    int (*dev_probe)  (device_t *);
    int (*dev_init)   (device_t *);
    int (*dev_uninit) (device_t *);
    int (*drv_init)   (driver_t *);
    int (*drv_uninit) (driver_t *);
    void *drv_api;
    void *driver_pv;
}driver_t;

/* Device structure */
typedef struct device_t
{
    list_node_t  dev_node;
    char        *dev_name;
    char        *dev_type;
    driver_t    *drv;
    device_t    *parent;
    void        *dev_data;
    uint32_t     flags;
    uint32_t     index;
    list_head_t  children;
}device_t;

typedef struct dev_srch_t
{
    device_t **stack;
    uint32_t stack_index;
    list_node_t *this_node;
    list_node_t *next_node;
    char *dev_name;
}dev_srch_t;


int devmgr_init(void);
int devmgr_add_drv(driver_t *drv);
int devmgr_remove_drv(driver_t *drv);
static int devmgr_add_device_to_drv
(
    device_t *dev, 
    const driver_t *drv
);

int devmgr_init(void);
int devmgr_dev_create(device_t **dev);
int devmgr_dev_delete(device_t *dev);
int devmgr_dev_add(device_t *dev, device_t *parent);
int devmgr_drv_add(driver_t *drv);
int devmgr_drv_remove(driver_t *drv);
char *devmgr_dev_type_get(device_t *dev);
char *devmgr_drv_type_get(driver_t *drv);
int devmgr_dev_type_set(device_t *dev, char *type);
int devmgr_dev_type_match(device_t *dev, char *type);
driver_t *devmgr_drv_find(const char *name);
int devmgr_drv_init(driver_t *drv);
int devmgr_dev_probe(device_t *dev);
int devmgr_dev_init(device_t *dev);
int devmgr_dev_uninit(device_t *dev);
int devmgr_drv_data_set(driver_t *drv, void *data);
void *devmgr_drv_data_get(const driver_t *drv);
int devmgr_dev_data_set(device_t *dev, void *data);
void *devmgr_dev_data_get(const device_t *dev);
device_t *devmgr_parent_get(const device_t *dev);
char *devmgr_dev_name_get(const device_t *dev);
int devmgr_dev_name_match(const device_t *dev, char *name);
int devmgr_dev_name_set(device_t *dev, char *name);
int devmgr_dev_index_set(device_t *dev, uint32_t index);
uint32_t devmgr_dev_index_get(device_t *dev);
void *devmgr_dev_api_get(device_t *dev);
device_t *devmgr_dev_get_by_name(const char *name, const uint32_t index);
int devmgr_dev_end(dev_srch_t *sh);
device_t *devmgr_dev_next(dev_srch_t *sh);
dev_srch_t *devmgr_dev_first(char *name, device_t **dev);
void *devmgr_drv_api_get(driver_t *drv);
driver_t *devmgr_dev_drv_get(device_t *dev);
#endif