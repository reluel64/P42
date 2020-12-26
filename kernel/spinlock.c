/* 
 * Spinlock 
 */ 

#include <spinlock.h>
#include <cpu.h>

void spinlock_init(spinlock_t *s)
{
    s->lock       = 0;
}

void spinlock_rw_init(spinlock_rw_t *s)
{
    s->lock = UINT32_MAX;
}

void spinlock_lock(spinlock_t *s)
{  
    int expected = 0;

    cpu_int_lock();
    
    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_ACQUIRE, 
                                      __ATOMIC_RELAXED)
         )
    {
        expected = 0;
        cpu_pause();
    }
}

void spinlock_unlock(spinlock_t *s)
{
    int expected = 1;
    
    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_RELEASE, 
                               __ATOMIC_RELAXED);

   cpu_int_unlock();
}

void spinlock_lock_int(spinlock_t *s, int *state)
{
    int expected = 0;

    *state = cpu_int_check();

    if(*state)
        cpu_int_lock();

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_RELAXED)
         )
    {
        expected = 0;
        cpu_pause();
    }
}

void spinlock_unlock_int(spinlock_t *s, int state)
{
    int expected = 1;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_RELEASE, 
                               __ATOMIC_RELAXED);

    if(state)
        cpu_int_unlock();
}
#if 0
void spinlock_read_lock_int(spinlock_t *s, int *state)
{
    int expected = 0;

    *state = cpu_int_check();

    if(*state)
        cpu_int_lock();

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_RELAXED)
         )
    {
        expected = 0;
        cpu_pause();
    }
}

void spinlock_read_unlock_int(spinlock_t *s, int state)
{
    int expected = 1;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_RELEASE, 
                               __ATOMIC_RELAXED);

    if(state)
        cpu_int_unlock();
}

void spinlock_write_lock_int(spinlock_t *s, int *state)
{
    int expected = 0;

    *state = cpu_int_check();

    if(*state)
        cpu_int_lock();

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_RELAXED)
         )
    {
        expected = 0;
        cpu_pause();
    }
}

void spinlock_write_unlock_int(spinlock_t *s, int state)
{
    int expected = 1;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_RELEASE, 
                               __ATOMIC_RELAXED);

    if(state)
        cpu_int_unlock();
}
#endif