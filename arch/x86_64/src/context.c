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
    virt_size_t   rsp0       = 0;

   // owner = th->owner;
   // vm_ctx = owner->vm_ctx;

    th->context = (virt_addr_t)kcalloc(1, CONTEXT_SIZE);

    if(th->context == 0)
    {
        return(-1);
    }
    
    /* allocate the RSP0 that will be used when switching 
     * from user mode to kernel mode
     */
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

    context[RIP_INDEX]  = (virt_addr_t)sched_thread_entry_point;
    context[RSP_INDEX]  = th->stack_origin + th->stack_sz;
    context[RBP_INDEX]  = th->stack_origin + th->stack_sz;
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

    if(prev != NULL)
    {
        __context_switch(prev->context, next->context);
    }
    else if(next != NULL)
    {
        __context_switch(0, next->context);
    }
    else
    {
        kprintf("NOT SWITCHING TO/FROM ANYTHING\n");
    }
}