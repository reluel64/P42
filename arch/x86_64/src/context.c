#include <scheduler.h>
#include <context.h>>
#include <platform.h>
#include <vm.h>
#include <utils.h>
extern void __context_save(void *tcb);
extern void __context_load(void *tcb);

int context_init
(
    sched_thread_t *th
)
{
    sched_owner_t *owner     = NULL;
    virt_addr_t   stack      = 0;
    virt_size_t   stack_size = 0;
    virt_addr_t   *context   = NULL;
    vm_ctx_t      *vm_ctx    = NULL;

   // owner = th->owner;
   // vm_ctx = owner->vm_ctx;

    th->context = (void*)vm_alloc(NULL,
                            VM_BASE_AUTO,
                            PAGE_SIZE,
                            0,
                            VM_ATTR_WRITABLE);

    if(th->context == NULL)
        return(-1);

    stack_size = ALIGN_UP(th->stack_sz, PAGE_SIZE);

    stack = vm_alloc(NULL,
                    VM_BASE_AUTO,
                    stack_size,
                    0,
                    VM_ATTR_WRITABLE);


    if(stack == 0)
    {
        vm_free(NULL, th->context, PAGE_SIZE);
        return(-1);
    }

    context = th->context;
    kprintf("CONTEXT %x\n",context);
    memset(context, 0, PAGE_SIZE);
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

    th->stack_sz  = stack_size;
    th->stack     = stack;
    th->stack_end = stack_size + stack; 

    context[RIP_INDEX] = (virt_addr_t*)th->entry_point;
    context[RSP_INDEX] = th->stack_end - 8;
    context[RBP_INDEX] = context[RSP_INDEX];
    context[CR3_INDEX] = __read_cr3();


    *((virt_addr_t*)(th->stack_end - 8)) = (virt_addr_t)th->entry_point;
    kprintf("ENTRY POINT %x\n", th->entry_point);
    return(0);
}


void context_save(sched_thread_t *th)
{
    __context_save(th->context);
}

void context_load(sched_thread_t *th)
{
    __context_load(th->context);
}