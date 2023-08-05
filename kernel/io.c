#include <liballoc.h>
#include <stdint.h>
#include <stddef.h>
#include <linked_list.h>
#include <spinlock.h>
#include <utils.h>
#include <io.h>


static list_head_t io_entries;
static spinlock_t  io_entry_lock;
static io_open_fd_t  opened[128];
static spinlock_t  opened_fd_lock;

int io_entry_register
(
    io_entry_t *entry
)
{
    uint8_t int_status = 0;
    int status = -1;

    if(entry != NULL)
    {

        spinlock_write_lock_int(&io_entry_lock, &int_status);

        if(linked_list_find_node(&io_entries, &entry->node) == -1)
        {
            linked_list_add_tail(&io_entries, &entry->node);
            status = 0;
        }

        spinlock_write_unlock_int(&io_entry_lock, int_status);
    }

    return(status);
}

static io_entry_t *io_find_entry
(
    char *entry_name
)
{
    uint8_t int_status = 0;
    list_node_t *node = NULL;
    io_entry_t *ent = NULL;

    if(entry_name != NULL)
    {
        spinlock_read_lock_int(&io_entry_lock, &int_status);

        node = linked_list_first(&io_entries);

        while(node)
        {
            ent = (io_entry_t *)node;

            if(ent->name != NULL)
            {
                if(strcmp(ent->name, entry_name) == 0)
                {
                    break;
                }
            }

            node = linked_list_next(node);
            ent = NULL;
        }

        spinlock_read_unlock_int(&io_entry_lock, int_status);
    }

    return(ent); 
}


int open
(
    char *name,
    int flags,
    int mode
)
{
    io_entry_t   *ie = NULL;
    io_open_fd_t *avail_fd = NULL;
    int          fd = -1;
    int          open_status = 0;
    char         *path = NULL;
    size_t       path_len = 0;
    
    if(name == NULL)
    {
        return(fd);
    }

    path_len = strlen(name);

    path = kcalloc(path_len + 1, 1);

    if(path == NULL)
    {
        return(fd);
    }

    ie = io_find_entry(name);

    while(fd < 128)
    {
        if(opened[fd].ent == NULL)
        {
            avail_fd = opened + fd;
            break;
        }
        fd++;
    }

    if(ie != NULL)
    {
        open_status = ie->open_func(&avail_fd->fd_data, 
                                    path, 
                                    flags, 
                                    mode);

        if(open_status == 0)
        {
            avail_fd->path = path;
            avail_fd->path_len = path_len;
            avail_fd->ent = ie;
        }
        else
        {
            fd = -1;
        }
    }
    else
    {
        fd = -1;
    }

    if(fd == -1)
    {
        kfree(path);
    }
    

    return(fd);
}

size_t write
(
    int         fd,
    const void *buf,
    size_t      length
)
{
    io_open_fd_t *opened_fd = NULL;
    io_entry_t   *fd_entry = NULL;
    size_t      bytes_written = -1;

    opened_fd = opened + fd;
    fd_entry = opened_fd->ent;

    if(fd_entry == NULL)
    {
        return(-1);
    }

    if(fd_entry->write_func != NULL)
    {
        bytes_written = fd_entry->write_func(opened_fd->fd_data, 
                                             buf, 
                                             length);
    }

    return(bytes_written);
}

size_t read
(
    int fd,
    void *buf,
    size_t length
)
{
    io_open_fd_t *opened_fd = NULL;
    io_entry_t   *fd_entry = NULL;
    size_t      bytes_read = -1;

    opened_fd = opened + fd;
    fd_entry = opened_fd->ent;

    if(fd_entry == NULL)
    {
        return(-1);
    }

    if(fd_entry->read_func != NULL)
    {
        bytes_read = fd_entry->read_func(opened_fd->fd_data,
                                         buf, 
                                         length);
    }

    return(bytes_read);
}

int io_init
(
    void
)
{
    linked_list_init(&io_entries);
    spinlock_rw_init(&io_entry_lock);
    spinlock_rw_init(&opened_fd_lock);
    memset(opened, 0, sizeof(opened));
    return(0);
}



