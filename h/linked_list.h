
#ifndef _linked_list_h
#define _linked_list_h

#include <stdint.h>
#include <stddef.h>

#define LINKED_LIST_INIT {.list.next = NULL, .list.prev = NULL, .count = 0}

struct list_node
{
    struct list_node *prev;
    struct list_node *next;
};

struct list_head
{
    struct list_node list;
    size_t count;
};

int linked_list_init
(
    struct list_head *lh
);

int linked_list_add_head
(
    struct list_head *lh, 
    struct list_node *ln
);

int linked_list_add_tail
(
    struct list_head *lh, 
    struct list_node *ln
);

int linked_list_add_before
(
    struct list_head *lh, 
    struct list_node *bn, 
    struct list_node *nn
);

int linked_list_add_after
(
    struct list_head *lh, 
    struct list_node *an, 
    struct list_node *nn
);

int linked_list_remove
(
    struct list_head *lh, 
    struct list_node *ln
);

int linked_list_find_node
(
    struct list_head *lh, 
    struct list_node *ln
);

struct list_node *linked_list_first
(
    struct list_head *lh
);

struct list_node *linked_list_next
(
    struct list_node *ln
);

struct list_node *linked_list_last
(
    struct list_head *lh
);

struct list_node *linked_list_prev
(
    struct list_node *ln
);

size_t linked_list_count
(
    struct list_head *lh
);

struct list_node * linked_list_get_first
(
    struct list_head *lh
);

struct list_node * linked_list_get_last
(
    struct list_head *lh
);

int32_t linked_list_concat
(
    struct list_head *src,
    struct list_head *dst
);

#endif