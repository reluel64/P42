#include <stdint.h>
#include <stddef.h>
typedef struct rb_node;

typedef int (*rb_tree_cmp)(void *tree_node, void *key);

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
    uint32_t node_count;
    rb_node_t *root;
}rb_tree_t;

void rb_left_rotate(rb_tree_t *tree, rb_node_t *node)
{
    rb_node_t *y = node->right;
    
    node->right = y->left;

    if(y->left != NULL)
    {
        y->left->parent = node;
    }

    y->parent = node->parent;

    if(node->parent == NULL)
    {
        tree->root = y;
    }
    else if(node == node->parent->left)
    {
        node->parent->left = y;
    }
    else
    {
        node->parent->right = y; 
    }

    y->left = node;
    node->parent = y;
}

void rb_right_rotate
(
    rb_tree_t *tree, 
    rb_node_t *node
)
{
    rb_node_t *y = node->left;
    
    node->left = y->right;

    if(y->right != NULL)
    {
        y->right->parent = node;
    }

    y->parent = node->parent;

    if(node->parent == NULL)
    {
        tree->root = y;
    }
    else if(node == node->parent->right)
    {
        node->parent->right = y;
    }
    else
    {
        node->parent->left = y; 
    }

    y->right = node;
    node->parent = y;
}



rb_node_t *rb_tree_min
(
    rb_node_t *node
)
{
    rb_node_t *min = NULL;

    min = node;

    if(min != NULL)
    {
        while(min->left != NULL)
        {
            min = min->left;
        }
    }

    return(min);
}

rb_node_t *rb_tree_max
(
    rb_node_t *node
)
{
    rb_node_t *max = NULL;

    max = node;

    if(max != NULL)
    {
        while(max->right != NULL)
        {
            max = max->right;
        }
    }
    
    return(max);
}

void rb_tree_insert_fixup
(
    rb_tree_t *tree,
    rb_node_t *node
)
{
    rb_node_t *y = NULL;

    while(node->parent->color == RED)
    {
        if(node->parent == node->parent->parent->left)
        {
            y = node->parent->parent->right;

            if(y->color == RED)
            {
                node->parent->color = BLACK;
                y->color = BLACK;
                node->parent->parent->color = RED;
                node = node->parent->parent;
            }
            else
            {
                if(node == node ->parent->right)
                {
                    node = node->parent;
                    rb_left_rotate(tree, node);
                }

                node->parent->color = BLACK;
                node->parent->parent->color = RED;
                rb_right_rotate(tree, node->parent->parent);
            }
        }
        else
        {
            y = node->parent->parent->right;

            if(y->color == RED)
            {
                node->parent->color = BLACK;
                y->color = BLACK;
                node->parent->parent->color = RED;
                node = node->parent->parent;
            }
            else
            {
                if(node == node->parent->left)
                {
                    node = node->parent;
                    rb_right_rotate(tree, node);
                }

                node->parent->color = BLACK;
                node->parent->parent->color = RED;
                rb_left_rotate(tree, node->parent->parent);
            }
        }
    }

    tree->root->color = BLACK;
}

void rb_transplant
(
    rb_tree_t *tree,
    rb_node_t *node_1,
    rb_node_t *node_2
)
{
    if(node_1->parent == NULL)
    {
        tree->root = node_2;
    }
    else if(node_1 == node_1->parent->left)
    {
        node_1->parent->left = node_2;
    }
    else
    {
        node_1->parent->right = node_2;
    }

    node_2->parent = node_1->parent;
}

uint32_t rb_tree_delete_fixup
(
    rb_tree_t *tree,
    rb_node_t *node
)
{
    rb_node_t *w = NULL;

    while((node != tree->root) && node->color == BLACK)
    {
        if(node == node->parent->left)
        {
            w = node->parent->right;

            if(w->color == RED)
            {
                w->color = BLACK;
                node->parent->color = RED;
                rb_left_rotate(tree, node->parent);
                w = node->parent->right;
            }

            if((w->left->color == BLACK) && (w->right->color == BLACK))
            {
                w->color = RED;
                node = node->parent;
            }
            else
            {
                if(w->right->color == BLACK)
                {
                    w->left->color = BLACK;
                    w->color = RED;
                    rb_right_rotate(tree, w);
                    w = node->parent->right;
                }

                w->color = node->parent->color;
                node->parent->color = BLACK;
                w->right->color = BLACK;

                rb_left_rotate(tree, node->parent);
                node = tree->root;
            }
        }
        else
        {
            w = node->parent->left;

            if(w->color == RED)
            {
                w->color = BLACK;
                node->parent->color = RED;
                rb_right_rotate(tree, node->parent);
                w = node->parent->left;
            }

            if((w->right->color == BLACK) && (w->left->color == BLACK))
            {
                w->color = RED;
                node = node->parent;
            }
            else
            {
                if(w->left->color == BLACK)
                {
                    w->right->color = BLACK;
                    w->color = RED;
                    rb_left_rotate(tree, w);
                    w = node->parent->left;
                }

                w->color = node->parent->color;
                node->parent->color = BLACK;
                w->left->color = BLACK;
                rb_right_rotate(tree, node->parent);
                node = tree->root;
            }
        }
    }

    node->color = BLACK;
}

uint32_t rb_tree_insert
(
    rb_tree_t *tree, 
    rb_tree_cmp func,
    void *key,
    rb_node_t *node
)
{
    rb_node_t *x = NULL;
    rb_node_t *y = NULL;
    uint32_t  status = 0;
    int cmp_result = 0;

    x = tree->root;

    while(x != NULL)
    {
        y = x;

        cmp_result = func(x, key);

        /* already in the tree - just break*/
        if(cmp_result == 0)
        {
            return(-1);
        }
        else if(cmp_result < 0)
        {
            x = x->left;
        }
        else
        {
            x = x->right;
        }
    }

    node->parent = y;

    if(y == NULL)
    {
        tree->root = node;
    }
    else if(cmp_result < 0)
    {
        y->left = node;
    }
    else
    {
        y->right = node;
    }

    node->left = NULL;
    node->right = NULL;
    node->color = RED;

    rb_tree_insert_fixup(tree, node);
}

uint32_t rb_tree_delete
(
    rb_tree_t *tree,
    rb_node_t *node
)
{
    rb_node_t *y = NULL;
    rb_node_t *x = NULL;

    rb_color_t orig_color = BLACK;

    y = node;
    orig_color = y->color;

    if(node->left == NULL)
    {
        x = node->right;
        rb_transplant(tree, node, node->right);
    }
    else if(node->right == NULL)
    {
        x = node->left;
        rb_transplant(tree, node, node->left);
    }
    else
    {
        y = rb_tree_min(node->right);
        orig_color = y->color;
        x = y->right;

        if(y != node->right)
        {
            rb_transplant(tree, y, y->right);
            y->right = node->right;
            y->right->parent = y;
        }
        else
        {
            x->parent = y;
            rb_transplant(tree, node, y);
            y->left = node->left;
            y->left->parent = y;
            y->color = node->color;
        }
    }

    if(orig_color == BLACK)
    {
        rb_tree_delete_fixup(tree, x);
    }
}

uint32_t rb_tree_find
(
    rb_tree_t *tree, 
    rb_tree_cmp cmp_func, 
    void *key,
    rb_node_t **result
)
{
    rb_node_t *node = NULL;
    int cmp_result = 0;
    uint32_t status = -1;

    if(tree == NULL || cmp_func == NULL || result == NULL)
    {
        return(-1);
    }

    node = tree->root;

    while(node != NULL)
    {
        cmp_result = cmp_func(node, key);

        if(cmp_result == 0)
        {
            *result = node;
            status = 0;
        }
        else if(cmp_result > 0)
        {
            node = node->left;
        }
        else 
        {
            node = node->right;
        }
    }

    return(status);
}