#ifndef rbtree_h
#define rbtree_h

#include <stdint.h>
#include <stddef.h>

typedef enum
{
    BLACK,
    RED
}rb_color_t;

struct rb_node
{
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    rb_color_t color;
};

struct rb_tree
{
    struct rb_node *root;
    struct rb_node nil;
};

typedef int (*rb_tree_commpare)(struct rb_node *tree_node, void *key);

void rb_tree_init
(
    struct rb_tree *t
);

int rb_delete
(
    struct rb_tree *t,
    struct rb_node *z
);

int rb_insert
(
    struct rb_tree *t,
    struct rb_node *z,
    rb_tree_commpare cmp,
    void *cmp_pv
);

#endif