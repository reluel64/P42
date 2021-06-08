#ifndef vm_extent_h
#define vm_extent_h

int vm_extent_split
(
    vm_extent_t *src,
    const virt_addr_t virt,
    const virt_size_t len,
    vm_extent_t *dst
);

int vm_extent_extract
(
    list_head_t *lh,
    uint32_t    ext_per_slot,
    vm_extent_t *ext
);

int vm_extent_insert
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    const vm_extent_t *ext
);

int vm_extent_alloc_slot
(
    vm_ctx_t *ctx, 
    list_head_t *lh,
    uint32_t ext_per_slot
);

#endif