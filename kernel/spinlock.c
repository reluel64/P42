/* 
 * Spinlock 
 */ 

#include <spinlock.h>
#include <cpu.h>

extern void __pause();
extern void __wbinvd();

void spinlock_init(spinlock_t *s)
{
    s->int_status = 0;
    s->lock       = 0;
}

void spinlock_lock(spinlock_t *s)
{  
    cpu_int_lock();

    while(!__sync_bool_compare_and_swap(&s->lock, 0, 1))
    {
        __pause();
    }
}

void spinlock_unlock(spinlock_t *s)
{
   __sync_bool_compare_and_swap(&s->lock, 1, 0);
   cpu_int_unlock();
}

void spinlock_lock_interrupt(spinlock_t *s, int *state)
{
    *state = cpu_int_check();

    if(*state)
        cpu_int_lock();

    while(!__sync_bool_compare_and_swap(&s->lock, 0, 1))
    {
        __pause();
    }
}

void spinlock_unlock_interrupt(spinlock_t *s, int state)
{
     __sync_bool_compare_and_swap(&s->lock, 1, 0);

    if(state)
        cpu_int_unlock();

}
