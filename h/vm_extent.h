#ifndef vm_extent_h
#define vm_extent_h

int vm_extent_split
(
    vm_extent_t *ext_left,
    const virt_addr_t virt_mid,
    const virt_size_t len_mid,
    vm_extent_t *ext_right
);

int vm_extent_join
(
    const vm_extent_t *src,
    vm_extent_t *dest
);

int vm_extent_extract
(
    list_head_t *lh,
    vm_extent_t *ext
);

int vm_extent_insert
(
    list_head_t *lh,
    const vm_extent_t *ext
);

int vm_extent_alloc_slot
(
    list_head_t *lh,
    uint32_t ext_per_slot
);

int vm_extent_merge
(
    list_head_t *lh
);


void vm_extent_header_init
(
    vm_extent_hdr_t *hdr,
    uint32_t extent_count
);

void vm_extent_copy
(
    vm_extent_t *dst,
    const vm_extent_t *src
);

#define VM_SLOT_SIZE (PAGE_SIZE)

#define VM_EXTENT_TO_HEADER(entry)         (void*)(((virt_addr_t)(entry)) - \
                                              (((virt_addr_t)(entry)) %  \
                                              VM_SLOT_SIZE ))

#endif