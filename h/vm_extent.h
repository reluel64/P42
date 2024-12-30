#ifndef vm_extent_h
#define vm_extent_h

int vm_extent_split
(
    struct vm_extent *ext_left,
    const virt_addr_t virt_mid,
    const virt_size_t len_mid,
    struct vm_extent *ext_right
);

int vm_extent_join
(
    const struct vm_extent *src,
    struct vm_extent *dest
);

int vm_extent_extract
(
    struct list_head *lh,
    struct vm_extent *ext
);

int vm_extent_insert
(
    struct list_head *lh,
    const struct vm_extent *ext
);

int vm_extent_alloc_slot
(
    struct list_head *lh,
    uint32_t ext_per_slot
);

int vm_extent_merge
(
    struct list_head *lh
);


void vm_extent_header_init
(
    struct vm_extent_hdr *hdr,
    uint32_t extent_count
);

void vm_extent_copy
(
    struct vm_extent *dst,
    const struct vm_extent *src
);

#define VM_SLOT_SIZE (PAGE_SIZE)

#define VM_EXTENT_TO_HEADER(entry)         (void*)(((virt_addr_t)(entry)) - \
                                              (((virt_addr_t)(entry)) %  \
                                              VM_SLOT_SIZE ))

#endif