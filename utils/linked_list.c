#include <stddef.h>
#include <linked_list.h>



/*
 * linked_list_init - initializes list head 
 */

int32_t linked_list_init
(
    struct list_head *lh
)
{
    if(lh == NULL)
    {
        return(-1);
    }

    lh->list.next  = NULL;
    lh->list.prev  = NULL;
    lh->count      = 0;
    
    return(0);
}

int32_t linked_list_add_head
(
    struct list_head *lh, 
    struct list_node *ln
)
{
   if(lh->list.prev == NULL && lh->list.next == NULL)
   {
        /* mark the node as head and tail */
        lh->list.next = lh->list.prev = ln;
        
        /* This is the only entry at the moment */
        ln->next = NULL;
        ln->prev = NULL;
   }
   else
   {
        ln->next       = lh->list.next;
        ln->next->prev = ln;
        ln->prev       = NULL;
        lh->list.next  = ln;
   }

   lh->count++;

   return(0);
}

int32_t linked_list_add_tail
(
    struct list_head *lh, 
    struct list_node *ln
)
{
    if(lh->list.next == NULL && lh->list.prev == NULL)
    {
        /* mark the node as head and tail */
        lh->list.next = lh->list.prev = ln;
        
        /* This is the only entry at the moment */
        ln->next = NULL;
        ln->prev = NULL;
    }
    else
    {
        ln->next       = NULL;
        ln->prev       = lh->list.prev;
        ln->prev->next = ln;
        lh->list.prev  = ln;
    }
    
    lh->count++;
    
    return(0);
}

int32_t linked_list_add_after
(
    struct list_head *lh, 
    struct list_node *an, 
    struct list_node *nn
)
{
    if(lh->list.prev == nn)
    {
        lh->list.prev = nn;
    }
    nn->next = an->next;
    nn->prev = an;
    an->next = nn;
    lh->count++;
    return(0);
}

int32_t linked_list_add_before
(
    struct list_head *lh, 
    struct list_node *bn, 
    struct list_node *nn
)
{
    if(lh->list.next == nn)
    {
        lh->list.next = nn;
    }
    nn->prev = bn->prev;
    nn->next = bn;
    bn->prev = nn;
    lh->count++;

    return(0);
}

int32_t linked_list_remove
(
    struct list_head *lh, 
    struct list_node *ln
)
{
    /* if this is the first node,
     *  then we will need to update the head
     */
    if(ln->prev == NULL)
    {
        lh->list.next = ln->next;
    }
    /* Otherwise, just relink the adjacent nodes */
    else
    {
        ln->prev->next = ln->next;
    }
    /* If this is the last node,
     * then the tail needs to be updated */
    if(ln->next == NULL)
    {
        lh->list.prev  = ln->prev;
    }
    /* Otherwise, relink the adjacent nodes */
    else
    {
        ln->next->prev = ln->prev;
    }
    
    lh->count--;

    return(0);
}

int32_t linked_list_find_node
(
    struct list_head *lh, 
    struct list_node *ln
)
{
    struct list_node *work_ln = NULL;

    work_ln = linked_list_first(lh);

    while(work_ln && work_ln != ln)
    {
        work_ln = linked_list_next(work_ln);
    }
    
    return((work_ln == NULL) ? -1 : 0);
}

int32_t linked_list_concat
(
    struct list_head *src,
    struct list_head *dst
)
{
    if(src->count == 0)
    {
        return(-1);
    }

    if(dst->count == 0)
    {
        *dst = *src;
    }
    else
    {
        dst->list.prev->next = src->list.next;
        src->list.next->prev = dst->list.prev;
        dst->list.prev = src->list.prev;
        dst->count += src->count;
    }

    linked_list_init(src);

    return(0);
}

struct list_node *linked_list_first
(
    struct list_head *lh
)
{
    return(lh->list.next);
}

struct list_node *linked_list_next
(
    struct list_node *ln
)
{
    return(ln->next);
}

struct list_node *linked_list_last
(
    struct list_head *lh
)
{
    return(lh->list.prev);
}

struct list_node *linked_list_prev
(
    struct list_node *ln
)
{
    return(ln->prev);
}

size_t linked_list_count
(
    struct list_head *lh
)
{
    return(lh->count);
}

struct list_node * linked_list_get_first
(
    struct list_head *lh
)
{
    struct list_node *ln = NULL;

    if(lh->list.next != NULL)
    {
        ln = lh->list.next;

        if(ln->next != NULL)
        {
            ln->next->prev = NULL;
        }
        else
        {
            lh->list.prev = NULL;
        }

        lh->list.next = ln->next;
        lh->count--;
    }

    return(ln);
}

struct list_node * linked_list_get_last
(
    struct list_head *lh
)
{
    struct list_node *ln = NULL;
    
    if(lh->list.prev != NULL)
    {
        ln = lh->list.prev;
        
        if(ln->prev != NULL)
        {
            ln->prev->next = NULL;        
        }
        else
        {
            lh->list.next = NULL;
        }

        lh->list.prev = ln->prev;

        lh->count--;
    }

    return(ln);
}