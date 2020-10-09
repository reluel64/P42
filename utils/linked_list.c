#include <stddef.h>
#include <linked_list.h>

/*
 * linked_list_init - initializes list head 
 */

int linked_list_init(list_head_t *lh)
{
   if(lh == NULL)
    return(-1);
    
    lh->list.next  = NULL;
    lh->list.prev  = NULL;
    lh->count      = 0;
    
    return(0);
}

int linked_list_add_head(list_head_t *lh, list_node_t *ln)
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

int linked_list_add_tail(list_head_t *lh, list_node_t *ln)
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

int linked_list_remove(list_head_t *lh, list_node_t *ln)
{
    if(ln->prev == NULL)
        lh->list.prev = ln->prev;
    
    else
        ln->prev->next = ln->next;
    
    if(ln->next == NULL)
        lh->list.next  = ln->next;
        
    else
        ln->next->prev = ln->prev;
    
    lh->count--;
}

int linked_list_find_node(list_head_t *lh, list_node_t *ln)
{
    list_node_t *work_ln = NULL;

    work_ln = linked_list_first(lh);

    while(work_ln)
    {
        if(work_ln == ln)
            return(0);
        
        work_ln = linked_list_next(work_ln);
    }

    return(-1);
}

list_node_t *linked_list_first(list_head_t *lh)
{
    return(lh->list.next);
}

list_node_t *linked_list_next(list_node_t *ln)
{
    return(ln->next);
}

list_node_t *linked_list_last(list_head_t *lh)
{
    return(lh->list.prev);
}

list_node_t *linked_list_prev(list_node_t *ln)
{
    return(ln->prev);
}

size_t linked_list_count(list_head_t *lh)
{
    return(lh->count);
}
