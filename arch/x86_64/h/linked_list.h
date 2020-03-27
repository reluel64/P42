
#ifndef _linked_list_h
#define _linked_list_h

#include <stdint.h>
#include <stddef.h>
typedef struct _list_node_t
{
    struct _list_node_t *prev;
    struct _list_node_t *next;
}list_node_t;

typedef struct
{
    size_t count;
    list_node_t list;
}list_head_t;

int linked_list_init(list_head_t *lh);
int linked_list_add_head(list_head_t *lh, list_node_t *ln);
int linked_list_add_tail(list_head_t *lh, list_node_t *ln);
int linked_list_remove(list_head_t *lh, list_node_t *ln);
list_node_t *linked_list_first(list_head_t *lh);
list_node_t *linked_list_next(list_node_t *ln);
list_node_t *linked_list_last(list_head_t *lh);
list_node_t *linked_list_prev(list_node_t *ln);
size_t linked_list_count(list_head_t *lh);
#endif