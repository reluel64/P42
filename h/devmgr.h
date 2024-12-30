#ifndef deviceh
#define deviceh

#include <linked_list.h>
#include <spinlock.h>

#define DEVMGR_DEV_INITIALIZED (1 << 0)
#define DEVMGR_DEV_PROBED      (1 << 1)
#define DEVMGR_DEV_ALLOCATED   (1 << 2)


#define DEVMGR_ROOT_BUS "root_bus_type"
struct driver_node;
struct device_node;

/* Driver structure */
struct driver_node
{
    struct list_node drv_node;
    char *drv_name;
    char *drv_type;
    int (*dev_probe)  (struct device_node *);
    int (*dev_init)   (struct device_node *);
    int (*dev_uninit) (struct device_node *);
    int (*drv_init)   (struct driver_node *);
    int (*drv_uninit) (struct driver_node *);
    void *drv_api;
    void *driver_pv;
};

/* Device structure */
struct device_node
{
    struct list_node  dev_node;
    char        *dev_name;
    char        *dev_type;
    struct driver_node    *drv;
    struct device_node    *parent;
    void        *dev_data;
    uint32_t     flags;
    uint32_t     index;
    struct list_head  children;
};

struct device_search_state
{
    struct device_node **stack;
    uint32_t stack_index;
    struct list_node *this_node;
    struct list_node *next_node;
    char *dev_name;
};

int devmgr_init(void);
int devmgr_add_drv(struct driver_node *drv);
int devmgr_remove_drv(struct driver_node *drv);


int devmgr_init(void);
int devmgr_dev_create(struct device_node **dev);
int devmgr_dev_delete(struct device_node *dev);
int devmgr_dev_add(struct device_node *dev, struct device_node *parent);
int devmgr_drv_add(struct driver_node *drv);
int devmgr_drv_remove(struct driver_node *drv);
char *devmgr_dev_type_get(struct device_node *dev);
char *devmgr_drv_type_get(struct driver_node *drv);
int devmgr_dev_type_set(struct device_node *dev, char *type);
int devmgr_dev_type_match(struct device_node *dev, char *type);
struct driver_node *devmgr_drv_find(const char *name);
int devmgr_drv_init(struct driver_node *drv);
int devmgr_dev_probe(struct device_node *dev);
int devmgr_dev_init(struct device_node *dev);
int devmgr_dev_uninit(struct device_node *dev);
int devmgr_drv_data_set(struct driver_node *drv, void *data);
void *devmgr_drv_data_get(const struct driver_node *drv);
int devmgr_dev_data_set(struct device_node *dev, void *data);
void *devmgr_dev_data_get(const struct device_node *dev);
struct device_node *devmgr_dev_parent_get(const struct device_node *dev);
char *devmgr_dev_name_get(const struct device_node *dev);
int devmgr_dev_name_match(const struct device_node *dev, char *name);
int devmgr_dev_name_set(struct device_node *dev, char *name);
int devmgr_dev_index_set(struct device_node *dev, uint32_t index);
uint32_t devmgr_dev_index_get(struct device_node *dev);
void *devmgr_dev_api_get(struct device_node *dev);
struct device_node *devmgr_dev_get_by_name(const char *name, const uint32_t index);
int devmgr_dev_end(struct device_search_state *sh);
struct device_node *devmgr_dev_next(struct device_search_state *sh);
struct device_search_state *devmgr_dev_first(char *name, struct device_node **dev);
void *devmgr_drv_api_get(struct driver_node *drv);
struct driver_node *devmgr_dev_drv_get(struct device_node *dev);
#endif