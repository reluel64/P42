#ifndef vm_extent_h
#define vm_extent_h

int vm_extent_split
(
    vm_extent_t *src,
    const virt_addr_t virt,
    const virt_size_t len,
    vm_extent_t *dst
);

int vm_extent_join
(
    vm_extent_t *src,
    vm_extent_t *dest
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
    vm_extent_t *ext
);

int vm_extent_alloc_slot
(
    list_head_t *lh,
    uint32_t ext_per_slot
);

int vm_extent_merge
(
    list_head_t *lh,
    uint32_t ext_per_slot
);

#define VM_SLOT_SIZE (PAGE_SIZE)

#define VM_EXTENT_TO_HEADER(entry)         (void*)(((virt_addr_t)(entry)) - \
                                              (((virt_addr_t)(entry)) %  \
                                              VM_SLOT_SIZE ))

#endif