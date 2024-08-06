#ifndef rbtree_h
#define rbtree_h

#include <stdint.h>
#include <stddef.h>

typedef enum
{
    BLACK,
    RED
}rb_color_t;

typedef struct rb_node
{
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    rb_color_t color;
}rb_node_t;

typedef struct rb_tree
{
    rb_node_t *root;
    rb_node_t nil;
}rb_tree_t;

typedef int (*rb_tree_commpare)(rb_node_t *tree_node, void *key);

void rb_tree_init
(
    rb_tree_t *t
);

int rb_delete
(
    rb_tree_t *t,
    rb_node_t *z
);

int rb_insert
(
    rb_tree_t *t,
    rb_node_t *z,
    rb_tree_commpare cmp,
    void *cmp_pv
);

#endif