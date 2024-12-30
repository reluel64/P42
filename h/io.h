#include <stdint.h>
#include <stddef.h>
#include <linked_list.h>
#include <spinlock.h>
#include <utils.h>

struct io_device_node
{
    struct list_node node;
    char    *name;
    int     (*open_func)(void **fd_data, char *path, int flags,   int mode);
    size_t  (*read_func)(void *fd_data,  void *buf,        size_t length);
    size_t  (*write_func)(void *fd_data, const void *buf,  size_t length);
    int     (*ioctl_func) (void *fd_data, int arg, void *arg_data);
    int     (*close_func)(void *fd_data);
    
};

struct io_file_descriptor
{
    struct list_node node;
    void *fd_data;
    struct io_device_node *ent;
    char *path;
    size_t path_len;
};

int io_init
(
    void
);

int io_entry_register
(
    struct io_device_node *entry
);

int open
(
    char *name,
    int flags,
    int mode
);

size_t read
(
    int fd,
    void *buf,
    size_t length
);

size_t write
(
    int fd,
    const void *buf,
    size_t length
);

int close
(
    int fd
);

int ioctl
(
    int fd,
    int arg,
    void *arg_data
);