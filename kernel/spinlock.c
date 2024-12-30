/* 
 * Spinlock 
 */ 

#include <spinlock.h>
#include <cpu.h>
#include <platform.h>

void spinlock_init
(
    struct spinlock *s
)
{
    s->lock       = 0;
}

void spinlock_rw_init
(
    struct spinlock_rw *s
)
{
    s->lock = UINT32_MAX;
}

void spinlock_lock
(
    struct spinlock *s
)
{  
    int expected = 0;    
    
    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_SEQ_CST)
         )
    {
        expected = 0;
        cpu_pause();
    }
}

int8_t spinlock_try_lock
(
    struct spinlock *s
)
{  
    int expected = 0;    
    int8_t rc = 0;
    if(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_SEQ_CST)
         )
    {
        expected = 0;
        rc = -1;
    }

    return(rc);
}

void spinlock_unlock
(
    struct spinlock *s
)
{
    int expected = 1;
    
    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_SEQ_CST, 
                               __ATOMIC_SEQ_CST);

}

void spinlock_lock_int
(
    struct spinlock *s, 
    uint8_t *flag
)
{
    int expected = 0;

    *flag = cpu_int_check();

    cpu_int_lock();

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_SEQ_CST)
         )
    {
        expected = 0;
        cpu_pause();
    }

}

int8_t spinlock_try_lock_int
(
    struct spinlock *s, 
    uint8_t *flag
)
{
    int expected = 0;
    int8_t rc = 0;

    *flag = cpu_int_check();

    cpu_int_lock();

    if(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 1, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_SEQ_CST)
         )
    {
        expected = 0;
        rc = 0xff;
        cpu_int_unlock();
    }

    return(rc);
}

void spinlock_unlock_int
(
    struct spinlock *s, 
    uint8_t flag
)
{
    uint32_t expected = 1;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, 0, 0, 
                               __ATOMIC_SEQ_CST, 
                               __ATOMIC_SEQ_CST);

    if(flag)
    {
        cpu_int_unlock();
    }
}

void spinlock_read_lock_int
(
    struct spinlock_rw *s, 
    uint8_t *flag
)
{
    *flag = cpu_int_check();
    cpu_int_lock();

    while(__atomic_load_n(&s->lock, __ATOMIC_SEQ_CST) == 0)
    {
        cpu_pause();
    }

    __atomic_sub_fetch(&s->lock, 1, __ATOMIC_SEQ_CST);    
}

void spinlock_read_unlock_int
(
    struct spinlock_rw *s, 
    uint8_t flag
)
{

    if(__atomic_load_n(&s->lock, __ATOMIC_SEQ_CST) < UINT32_MAX)
    {
        __atomic_add_fetch(&s->lock, 1, __ATOMIC_SEQ_CST);
    }
    
    if(flag)
    {
        cpu_int_unlock();
    }
}

void spinlock_write_lock_int
(
    struct spinlock_rw *s, 
    uint8_t *flag
)
{
    uint32_t expected = UINT32_MAX;

    *flag = cpu_int_check();
    cpu_int_lock();

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 0, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_SEQ_CST)
         )
    {
        expected = UINT32_MAX;
        cpu_pause();
    }
}

void spinlock_write_unlock_int
(
    struct spinlock_rw *s, 
    uint8_t flag
)
{
    uint32_t expected = 0;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, UINT32_MAX, 0, 
                               __ATOMIC_SEQ_CST, 
                               __ATOMIC_SEQ_CST);

    if(flag)
    {
        cpu_int_unlock();
    }
}

void spinlock_read_lock
(
    struct spinlock_rw *s
)
{

    while(__atomic_load_n(&s->lock, __ATOMIC_SEQ_CST) == 0)
    {
        cpu_pause();
    }

    __atomic_sub_fetch(&s->lock, 1, __ATOMIC_SEQ_CST);
    
}

void spinlock_read_unlock
(
    struct spinlock_rw *s
)
{

    if(__atomic_load_n(&s->lock, __ATOMIC_SEQ_CST) < UINT32_MAX)
        __atomic_add_fetch(&s->lock, 1, __ATOMIC_SEQ_CST);

}

void spinlock_write_lock
(
    struct spinlock_rw *s
)
{
    uint32_t expected = UINT32_MAX;

    while(!__atomic_compare_exchange_n(&s->lock, 
                                      &expected, 0, 0, 
                                      __ATOMIC_SEQ_CST, 
                                      __ATOMIC_SEQ_CST)
         )
    {
        expected = UINT32_MAX;
        cpu_pause();
    }
}

void spinlock_write_unlock
(
    struct spinlock_rw *s
)
{
    uint32_t expected = 0;

    __atomic_compare_exchange_n(&s->lock, 
                               &expected, UINT32_MAX, 0, 
                               __ATOMIC_SEQ_CST, 
                               __ATOMIC_SEQ_CST);

}