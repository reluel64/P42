#ifndef vm_space_h
#define vm_space_h

virt_addr_t vm_space_alloc
(
    struct vm_ctx *ctx,
    virt_addr_t addr,
    virt_size_t len,
    uint32_t flags,
    uint32_t eflags
);

int vm_space_free
(
    struct vm_ctx *ctx,
    virt_addr_t addr,
    virt_size_t len,
    uint32_t    *old_flags,
    uint32_t    *old_eflags
);

#endif