#ifndef schedh
#define schedh
#include <devmgr.h>
#include <cpu.h>

#define THREAD_READY   (0x0)
#define THREAD_RUNNING (0x1)
#define THREAD_BLOCKED (0x2)
#define THREAD_SLEEPING (0x4)


typedef struct sched_thread_t sched_thread_t;
typedef struct sched_exec_unit_t
{
    cpu_t            *cpu;                   /* cpu structure that is tied 
                                              * to the scheduler execution unit 
                                              */
    
    list_head_t       active_q;    /* queue of active threads on the current CPU*/
    list_head_t       blocked_q;   /* queue of blocked threads */
    list_head_t       sleep_q;
    list_head_t       dead_q;      /* queue of dead threads - for cleanup */
    sched_thread_t   *current;     /* current thread */
    sched_thread_t   *idle;        /* our dearest idle task */
    spinlock_t        lock;        /* lock to protect the queues */
    uint32_t          flags;       
    volatile uint32_t unblocked_th; /* unblocked thread count */
}sched_exec_unit_t;

typedef struct sched_thread_t
{
    list_node_t        node;        /* node in the queue                             */
    list_node_t        pend_node;  /* node for semaphore                            */
    uint32_t           flags;       /* thread flags                                  */
    void              *owner;       /* owner of the thread - if kernel, owner = null */
    void              *context;     /* platform dependent context                    */
    uint32_t           id;          /* thread id                                     */
    uint16_t           prio;        /* priority                                      */
    virt_addr_t        stack;       /* start of memory allocated for the stack       */
    virt_size_t        stack_sz;    /* stack size                                    */
    void              *entry_point; /* entry point of the thread                     */
    void              *pv;          /* parameter for the entry point of the thread   */
    sched_exec_unit_t *unit;        /* execution unit on which the thread is running */

    spinlock_t        lock;         /* lock to protect the structure members */
    uint32_t        sleeped;
    uint32_t        to_sleep;
}sched_thread_t;


int sched_cpu_init(device_t *timer, cpu_t *cpu);
int sched_init(void);
int sched_init_thread
(
    sched_thread_t    *th,
    void        *entry_pt,
    virt_size_t stack_sz,
    uint32_t    prio,
    void *pv
    
);
int sched_start_thread(sched_thread_t *th);
sched_thread_t *sched_thread_self(void);
void sched_unblock_thread(sched_thread_t *th);
void sched_block_thread(sched_thread_t *th);
#endif