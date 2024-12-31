/* Red-Black tree implementation */

#include <rb_tree.h>


uint32_t rb_tree_find
(
    struct rb_tree *tree, 
    rb_tree_commpare cmp_func, 
    void *key,
    struct rb_node **result
)
{
    struct rb_node *node = NULL;
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

static void rb_transplant
(
    struct rb_tree *t,
    struct rb_node *u,
    struct rb_node *v
)
{
    if(u->parent == &t->nil)
    {
        t->root = v;
    } 
    else if(u == u->parent->left)
    {
        u->parent->left = v;
    }
    else
    {
        u->parent->right = v;
    }

    v->parent = u->parent;
}

struct rb_node *rb_tree_minimum
(
    struct rb_tree *t,
    struct rb_node *x
)
{
    while(x->left != &t->nil)
    {
        x = x->left;
    }

    return(x);
}

struct rb_node *rb_tree_maximum
(
    struct rb_tree *t,
    struct rb_node *x
)
{
    while(x->right != &t->nil)
    {
        x = x->right;
    }

    return(x);
}

static void rb_left_rotate
(
    struct rb_tree *t,
    struct rb_node *x
)
{
    struct rb_node *y = NULL;

    y = x->right;
    x->right = y->left;

    if(y->left != &t->nil)
    {
        y->left->parent = x;
    }

    y->parent = x->parent;

    if(x->parent == &t->nil)
    {
        t->root = y;
    }
    else if(x == x->parent->left)
    {
        x->parent->left = y;
    }
    else
    {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;
}

static void rb_right_rotate
(
    struct rb_tree *t,
    struct rb_node *x
)
{
    struct rb_node *y = NULL;

    y = x->left;
    x->left = y->right;

    if(y->right != &t->nil)
    {
        y->right->parent = x;
    }

    y->parent = x->parent;

    if(x->parent == &t->nil)
    {
        t->root = y;
    }
    else if(x == x->parent->right)
    {
        x->parent->right = y;
    }
    else
    {
        x->parent->left = y;
    }

    y->right = x;
    x->parent = y;
}

static void rb_insert_fixup
(
    struct rb_tree *t,
    struct rb_node *z
)
{
    struct rb_node *y = NULL;
    
    while( z->parent->color == RED)
    {
        if(z->parent == z->parent->parent->left)
        {
            y = z->parent->parent->right;

            if(y->color == RED)
            {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            }
            else
            {
                if(z == z->parent->right)
                {
                    z = z->parent;
                    rb_left_rotate(t, z);
                }

                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rb_right_rotate(t, z->parent->parent);
            }
        }
        else
        {
            y = z->parent->parent->left;

            if(y->color == RED)
            {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            }
            else
            {
                if(z == z->parent->left)
                {
                    z = z->parent;
                    rb_right_rotate(t, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                rb_left_rotate(t, z->parent->parent);
            }
        }
    }

    t->root->color = BLACK;
}

static void rb_delete_fixup
(
    struct rb_tree *t,
    struct rb_node *x
)
{
    struct rb_node *w = NULL;

    while(x != t->root && x->color == BLACK)
    {
        if(x == x->parent->left)
        {
            w = x->parent->right;

            if(w->color == RED)
            {
                w->color = BLACK;
                x->parent->color = RED;
                rb_left_rotate(t, x->parent);
                w = x->parent->right;
            }

            if((w->left->color == BLACK) && (w->right->color == BLACK))
            {
                w->color = RED;
                x = x->parent;
            }
            else
            {
                if(w->right->color == BLACK)
                {
                    w->left->color = BLACK;
                    w->color = RED;
                    rb_right_rotate(t, w);
                    w = x->parent->right;
                }

                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                rb_left_rotate(t, x->parent);
                x = t->root;
            }
        }
        else
        {
            w = x->parent->left;

            if(w->color == RED)
            {
                w->color = BLACK;
                x->parent->color = RED;
                rb_right_rotate(t, x->parent);
                w = x->parent->left;
            }

            if((w->right->color == BLACK) && (w->left->color == BLACK))
            {
                w->color = RED;
                x = x->parent;
            }
            else
            {
                if(w->left->color == BLACK)
                {
                    w->right->color = BLACK;
                    w->color = RED;
                    rb_left_rotate(t, w);
                    w = x->parent->left;
                }

                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                rb_right_rotate(t, x->parent);
                x = t->root;
            }
        }
    }
}

int rb_insert
(
    struct rb_tree *t,
    struct rb_node *z,
    rb_tree_commpare cmp,
    void *cmp_pv
    
)
{
    struct rb_node *x = NULL;
    struct rb_node *y = NULL;
    int cmp_result = 0;
    int result = -1;
    uint8_t found = 0;
    x = t->root;
    y = &t->nil;


    while(x != &t->nil)
    {
        y = x;

        cmp_result = cmp(y, cmp_pv);

        if(cmp_result < 0)
        {
            x = x->left;
        }
        else
        {
            x = x->right;
        }
    }

    if(found == 0)
    {

        z->parent = y;
        z->left = &t->nil;
        z->right = &t->nil;
        z->color = RED;

        if(y == &t->nil)
        {
            t->root = z;
        }
        else if(cmp_result < 0)
        {
            y->left = z;
            
        }
        else if(cmp_result > 0)
        {
            y->right = z;
        }

        rb_insert_fixup(t, z);
        
        result = 0;
    }
   

    return(result);
}

int rb_delete
(
    struct rb_tree *t,
    struct rb_node *z
)
{
    struct rb_node *x = NULL;
    struct rb_node *y = NULL;
    enum rb_color y_orig_color = BLACK;

    y = z;
    y_orig_color = y->color;

    if(z->left == &t->nil)
    {
        x = z->right;
        rb_transplant(t, z, z->right);
    }
    else if(z->right == &t->nil)
    {
        x = z->left;
        rb_transplant(t, z, z->left);
    }
    else
    {
        y = rb_tree_minimum(t, z->right);
        y_orig_color = y->color;
        x = y->right;

        if(y != z->right)
        {
            rb_transplant(t,y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        else
        {
            x->parent = y;
        }

        rb_transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color =z->color;
    }

    if(y_orig_color == BLACK)
    {
        rb_delete_fixup(t, x);
    }

    return(0);
}

void rb_tree_init
(
    struct rb_tree *t
)
{
    t->nil.color = BLACK;
    t->nil.left = NULL;
    t->nil.right = NULL;
    t->nil.parent = NULL;
    t->root = &t->nil;
}