#include <scheduler.h>
#include <context.h>
#include <platform.h>
#include <vm.h>
#include <utils.h>
#include <liballoc.h>

extern void __context_switch
(   
    virt_addr_t ctx_prev, 
    virt_addr_t ctx_next
);

extern void __context_unit_start
(
    virt_addr_t ctx
);

void context_main
(
    virt_addr_t *context
)
{
    sched_thread_t *th = NULL;
    void *(*thread_main)(void *pv) = NULL;
    th = (sched_thread_t*)context[TH_INDEX];

    /* First thing to do is to unlock the unit on which we
     * are running 
     */

    thread_main = th->entry_point;

    spinlock_unlock(&th->unit->lock);    
    cpu_int_unlock();

    /* call the thread main routine */
    if(thread_main != NULL)
    {
        th->rval = thread_main(th->arg);
    }
    

}

int context_init
(
    sched_thread_t *th
)
{
    sched_owner_t *owner     = NULL;
    virt_addr_t   stack      = 0;
    virt_addr_t   *context   = NULL;
    virt_size_t   stack_size = 0;
    vm_ctx_t      *vm_ctx    = NULL;
    virt_size_t   rsp0       = 0;

   // owner = th->owner;
   // vm_ctx = owner->vm_ctx;

    th->context = (virt_addr_t)kcalloc(1, CONTEXT_SIZE);

    if(th->context == 0)
        return(-1);

    stack_size = ALIGN_UP(th->stack_sz, PAGE_SIZE);

    stack = vm_alloc(NULL,
                    VM_BASE_AUTO,
                    stack_size,
                    0,
                    VM_ATTR_WRITABLE);

    if(stack == VM_INVALID_ADDRESS)
    {
        kfree((void*)th->context);
        return(-1);
    }

    rsp0 = vm_alloc(NULL, VM_BASE_AUTO, PAGE_SIZE, 0, VM_ATTR_WRITABLE);

    if(rsp0 == VM_INVALID_ADDRESS)
    {
        vm_free(NULL, stack, stack_size);
        kfree((void*)th->context);
        return(-1);
    }

    memset((void*)rsp0, 0, PAGE_SIZE);

    /* Fill the context */
    context = (virt_addr_t*)th->context;

#if 0
    if(owner->user_space)
    {
        context[CS_INDEX] = 0x18;
        context[DS_INDEX] = 0x20;
    }
    else
#endif
    {
        context[CS_INDEX] = 0x8;
        context[DS_INDEX] = 0x10;
    }
    /* Set up stack info */
    th->stack_sz     = stack_size;
    th->stack_bottom = stack;
    th->stack_top    = stack_size + stack;

    context[RIP_INDEX]  = (virt_addr_t)sched_thread_entry_point;
    context[RSP_INDEX]  = th->stack_top;
    context[RBP_INDEX]  = th->stack_top;
    context[CR3_INDEX]  = __read_cr3();
    context[TH_INDEX ]  = (virt_addr_t)th;
    context[RSP0_INDEX] = rsp0;

    return(0);
}



void context_switch
(
    sched_thread_t *prev,
    sched_thread_t *next
)
{
    gdt_update_tss(next->unit->cpu->cpu_pv, 
                  ((virt_addr_t*)next->context)[RSP0_INDEX]);
    
    __context_switch(prev->context, next->context);
}

void context_unit_start
(
    sched_thread_t *th
)
{
        gdt_update_tss(th->unit->cpu->cpu_pv, 
                  ((virt_addr_t*)th->context)[RSP0_INDEX]);

    __context_switch(0, th->context);
}