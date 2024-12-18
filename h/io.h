#include <stdint.h>
#include <stddef.h>
#include <linked_list.h>
#include <spinlock.h>
#include <utils.h>

typedef struct io_entry_t
{
    list_node_t node;
    char    *name;
    int     (*open_func)(void **fd_data, char *path, int flags,   int mode);
    size_t  (*read_func)(void *fd_data,  void *buf,        size_t length);
    size_t  (*write_func)(void *fd_data, const void *buf,  size_t length);
    int     (*ioctl_func) (void *fd_data, int arg, void *arg_data);
    int     (*close_func)(void *fd_data);
    
}io_entry_t;

typedef struct
{
    list_node_t node;
    void *fd_data;
    io_entry_t *ent;
    char *path;
    size_t path_len;
}io_fd_desc_t;

int io_init
(
    void
);

int io_entry_register
(
    io_entry_t *entry
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