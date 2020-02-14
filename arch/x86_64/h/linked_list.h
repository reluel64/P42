
#ifndef _linked_list_h
#define _linked_list_h

#include <stdint.h>

typedef struct _list_node_t
{
    struct _list_node_t *prev;
    struct _list_node_t *next;
}list_node_t;

typedef struct
{
    uint64_t count;
    list_node_t list;
}list_head_t;

#endif