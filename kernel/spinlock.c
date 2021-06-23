/* 
 * Spinlock 
 */ 

#include <spinlock.h>
#include <cpu.h>
#include <platform.h>

void spinlock_init(spinlock_t *s)
{
    s->lock       = 0;
    s->int_lock_cnt   = 0;
    s->pre_lock_state = 0;
}

void spinlock_rw_init(spinlock_t *s)
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

void spinlock_lock_int(spinlock_t *s)
{
    int expected = 0;


    if(!__atomic_fetch_add(&s->int_lock_cnt, 1, __ATOMIC_ACQUIRE))
    {
        s->pre_lock_state = cpu_int_check();

        if(s->pre_lock_state)
            cpu_int_lock();
    }

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

void spinlock_unlock_int(spinlock_t *s)
{
    uint32_t expected = 1;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_RELEASE, 
                               __ATOMIC_RELAXED);


    if(__atomic_load_n(&s->int_lock_cnt, __ATOMIC_ACQUIRE) > 0)
    {
        if(!__atomic_sub_fetch(&s->int_lock_cnt, 1, __ATOMIC_ACQUIRE))
        {
            if(s->pre_lock_state)
                cpu_int_unlock();
        }
    }
}

void spinlock_read_lock_int(spinlock_t *s)
{

    if(!__atomic_fetch_add(&s->int_lock_cnt, 1, __ATOMIC_ACQUIRE))
    {
        s->pre_lock_state = cpu_int_check();

        if(s->pre_lock_state)
            cpu_int_lock();
    }

    while(!__atomic_load_n(&s->lock, __ATOMIC_ACQUIRE) > 0)
    {
        cpu_pause();
    }

    __atomic_sub_fetch(&s->lock, 1, __ATOMIC_ACQUIRE);
    
}

void spinlock_read_unlock_int(spinlock_t *s)
{

    if(__atomic_load_n(&s->lock, __ATOMIC_ACQUIRE) < UINT32_MAX)
        __atomic_add_fetch(&s->lock, 1, __ATOMIC_RELEASE);

    if(__atomic_load_n(&s->int_lock_cnt, __ATOMIC_ACQUIRE) > 0)
    {
        if(!__atomic_sub_fetch(&s->int_lock_cnt, 1, __ATOMIC_ACQUIRE))
        {
            if(s->pre_lock_state)
                cpu_int_unlock();
        }
    }
}

void spinlock_write_lock_int(spinlock_t *s)
{
    uint32_t expected = UINT32_MAX;

    if(!__atomic_fetch_add(&s->int_lock_cnt, 1, __ATOMIC_ACQUIRE))
    {
        s->pre_lock_state = cpu_int_check();

        if(s->pre_lock_state)
            cpu_int_lock();
    }

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 0, 0, 
                                      __ATOMIC_ACQUIRE, 
                                      __ATOMIC_RELAXED)
         )
    {
        expected = UINT32_MAX;
        cpu_pause();
    }
}

void spinlock_write_unlock_int(spinlock_t *s)
{
    uint32_t expected = 0;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, UINT32_MAX, 0, 
                               __ATOMIC_RELEASE, 
                               __ATOMIC_RELAXED);

    if(__atomic_load_n(&s->int_lock_cnt, __ATOMIC_ACQUIRE) > 0)
    {
        if(!__atomic_sub_fetch(&s->int_lock_cnt, 1, __ATOMIC_ACQUIRE))
        {
            if(s->pre_lock_state)
                cpu_int_unlock();
        }
    }
}
