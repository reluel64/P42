/* 
 * Spinlock 
 */ 



#include <spinlock.h>
#include <types.h>

extern void __sti();
extern void __cli();
extern int  __geti();
extern void __pause();
extern void __wbinvd();

void spinlock_init(spinlock_t *s)
{
    s->int_status = 0;
    s->lock       = 0;
}

void spinlock_lock(spinlock_t *s)
{   
    while(!__sync_bool_compare_and_swap(&s->lock, 0, 1))
    {
        __pause();
    }

}

int spinlock_try_lock(spinlock_t *s)
{
    return(!__sync_bool_compare_and_swap(&s->lock, 0, 1));
}

void spinlock_unlock(spinlock_t *s)
{
   __sync_bool_compare_and_swap(&s->lock, 1, 0);
}

void spinlock_lock_interrupt(spinlock_t *s)
{
    s->int_status = __geti();
    
    if(s->int_status)
        __cli();

    spinlock_lock(s);
}

void spinlock_unlock_interrupt(spinlock_t *s)
{
    spinlock_unlock(s);
  
    if(s->int_status)
        __sti();

}

int spinlock_try_lock_interrupt(spinlock_t *s)
{
    int status = 0;

     s->int_status = __geti();

     if(s->int_status)
        __cli();

    status = spinlock_try_lock(s);
    
    if(status && s->int_status)
        __sti();

    return(status);    
}