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

    if(stack == 0)
    {
        kfree((void*)th->context);
        return(-1);
    }

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

    context[RIP_INDEX] = (virt_addr_t)sched_thread_entry_point;
    context[RSP_INDEX] = th->stack_top;
    context[RBP_INDEX] = th->stack_top;
    context[CR3_INDEX] = __read_cr3();
    context[TH_INDEX]  = (virt_addr_t)th;


    return(0);
}

void context_switch
(
    sched_thread_t *prev,
    sched_thread_t *next
)
{
    sched_exec_unit_t *unit = NULL;
    virt_addr_t       *prev_ctx = NULL;
    virt_addr_t       *next_ctx = NULL;
    phys_addr_t        prev_cr3  = 0;

    __context_switch(prev->context, next->context);

}

void context_unit_start
(
    sched_thread_t *th
)
{
    __context_unit_start(th->context);
}