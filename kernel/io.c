#include <liballoc.h>
#include <stdint.h>
#include <stddef.h>
#include <linked_list.h>
#include <spinlock.h>
#include <utils.h>
#include <io.h>

#define MAX_FD_COUNT 128

static list_head_t    io_entries;
static spinlock_rw_t  io_entry_lock;
static io_fd_desc_t  fd_array[MAX_FD_COUNT];
static spinlock_t  opened_fd_lock;
static spinlock_t  avail_fd_lock;

static list_head_t opened_fd = LINKED_LIST_INIT;
static list_head_t avail_fd = LINKED_LIST_INIT;


int io_entry_register
(
    io_entry_t *entry
)
{
    uint8_t int_status = 0;
    int status = -1;

    if(entry != NULL)
    {
        /* require open, ioctl and close to be present at minimum */
        if((entry->open_func  != NULL)  && 
           (entry->ioctl_func != NULL)  && 
           (entry->close_func != NULL))
        {
            spinlock_write_lock_int(&io_entry_lock, &int_status);

            if(linked_list_find_node(&io_entries, &entry->node) == -1)
            {
                linked_list_add_tail(&io_entries, &entry->node);
                status = 0;
            }

            spinlock_write_unlock_int(&io_entry_lock, int_status);
        }
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
    io_fd_desc_t *fd_desc = NULL;
    int          fd = -1;
    int          open_status = 0;
    char         *path = NULL;
    size_t       path_len = 0;
    
    if(name != NULL)
    {
        path_len = strlen(name);
        path = kcalloc(path_len + 1, 1);
    }
    
    if(path != NULL)
    {
        spinlock_lock(&avail_fd_lock);
        fd_desc = (io_fd_desc_t*)linked_list_get_first(&avail_fd);
        spinlock_unlock(&avail_fd_lock);        
    }

    if(fd_desc != NULL)
    {
        ie = io_find_entry(name);

        if(ie != NULL)
        {
            open_status = ie->open_func(&fd_desc->fd_data, 
                                        path, 
                                        flags, 
                                        mode);

            if(open_status == 0)
            {
                
                fd_desc->path = path;
                fd_desc->path_len = path_len;
                fd_desc->ent = ie;
                fd = fd_desc - fd_array;

                spinlock_lock(&opened_fd_lock);
                linked_list_add_tail(&opened_fd, &fd_desc->node);
                spinlock_unlock(&opened_fd_lock);
            }
            else
            {
                spinlock_lock(&avail_fd_lock);
                linked_list_add_head(&avail_fd, &fd_desc->node);
                spinlock_unlock(&avail_fd_lock);     
            }
        }
        else
        {
            spinlock_lock(&avail_fd_lock);
            linked_list_add_head(&avail_fd, &fd_desc->node);
            spinlock_unlock(&avail_fd_lock);     
        }
    }

    if(fd == -1)
    {
        if(path != NULL)
        {
            kfree(path);
        }
    }
    

    return(fd);
}

int close
(
    int fd
)
{
    io_fd_desc_t *fd_desc = NULL;
    io_entry_t   *fd_entry = NULL;
    int st = -1;

    /* validate the file descriptor number */
    if(fd < MAX_FD_COUNT && fd >= 0)
    {
        fd_desc = fd_array + fd;

        /* check if file descriptor is opened */
        if(linked_list_find_node(&opened_fd, &fd_desc->node) == 0)
        {
            fd_entry = fd_desc->ent;
        }
    }

    if(fd_entry == NULL)
    {
        return(-1);
    }

    if(fd_entry->close_func != NULL)
    {

        /* first remove the descriptor to make sure
         * subsequent attempts to read/write/ioctl fail
         */       

        spinlock_lock(&opened_fd_lock);
        linked_list_add_tail(&opened_fd, &fd_desc->node);
        spinlock_unlock(&opened_fd_lock);
        
        st = fd_entry->close_func(fd_desc->fd_data);

        if(st == 0)
        {
            memset(fd_desc, 0, sizeof(io_fd_desc_t));

            spinlock_lock(&avail_fd_lock);
            linked_list_add_tail(&avail_fd, &fd_desc->node);
            spinlock_unlock(&avail_fd_lock);
        }
        else
        {
            spinlock_lock(&opened_fd_lock);
            linked_list_add_tail(&opened_fd, &fd_desc->node);
            spinlock_unlock(&opened_fd_lock);
        }

    }

    return(st);
}

size_t write
(
    int         fd,
    const void *buf,
    size_t      length
)
{
    io_fd_desc_t *fd_desc = NULL;
    io_entry_t   *fd_entry = NULL;
    size_t      bytes_written = -1;

    /* validate the file descriptor number */
    if(fd < MAX_FD_COUNT && fd >= 0)
    {
        fd_desc = fd_array + fd;

        /* check if file descriptor is opened */
        if(linked_list_find_node(&opened_fd, &fd_desc->node) == 0)
        {
            fd_entry = fd_desc->ent;
        }
    }

    if(fd_entry == NULL)
    {
        return(-1);
    }

    if(fd_entry->write_func != NULL)
    {
        bytes_written = fd_entry->write_func(fd_desc->fd_data, 
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
    io_fd_desc_t *fd_desc = NULL;
    io_entry_t   *fd_entry = NULL;
    size_t      bytes_read = -1;

    if(fd < MAX_FD_COUNT && fd >= 0)
    {
        fd_desc = fd_array + fd;
        /* validate the file descriptor */
        if(linked_list_find_node(&opened_fd, &fd_desc->node) == 0)
        {
            fd_entry = fd_desc->ent;
        }
    }

    if(fd_entry == NULL)
    {
        return(-1);
    }

    if(fd_entry->read_func != NULL)
    {
        bytes_read = fd_entry->read_func(fd_desc->fd_data,
                                         buf, 
                                         length);
    }

    return(bytes_read);
}

int ioctl
(
    int         fd,
    int         arg,
    void      *arg_data
)
{
    io_fd_desc_t *fd_desc = NULL;
    io_entry_t   *fd_entry = NULL;
    int status = -1;

    /* validate the file descriptor number */
    if(fd < MAX_FD_COUNT && fd >= 0)
    {
        fd_desc = fd_array + fd;

        /* check if file descriptor is opened */
        if(linked_list_find_node(&opened_fd, &fd_desc->node) == 0)
        {
            fd_entry = fd_desc->ent;
        }
    }

    if(fd_entry == NULL)
    {
        return(-1);
    }

    if(fd_entry->write_func != NULL)
    {
        status = fd_entry->ioctl_func(fd_desc->fd_data, 
                                             arg, 
                                             arg_data);
    }

    return(status);
}


int io_init
(
    void
)
{
    linked_list_init(&io_entries);
    linked_list_init(&avail_fd);
    linked_list_init(&opened_fd);
    spinlock_rw_init(&io_entry_lock);
    spinlock_init(&opened_fd_lock);
    spinlock_init(&avail_fd_lock);
    memset(fd_array, 0, sizeof(fd_array));
    
    for(uint32_t i = 0; i < MAX_FD_COUNT; i++)
    {
        linked_list_add_tail(&avail_fd, &fd_array[i].node);
    }

    return(0);
}



