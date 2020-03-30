#ifndef physmm_h
#define physmm_h
#include <stdint.h>
#include <defs.h>
#define PAGE_SIZE (0x1000)
#define PF_PER_ITEM (64)

#define LOW_MEMORY   (0x100000)
#define ALLOC_CONTIG  (1 << 0)
#define ALLOC_HIGHEST (1 << 2)
#define ALLOC_ISA_DMA (1 << 3)
#define ALLOC_CB_STOP (1 << 4)

typedef phys_size_t (*alloc_cb)(phys_addr_t phys, phys_size_t count, void *pv);
typedef int     (*free_cb) (phys_addr_t *phys, phys_size_t *count, void *pv);

typedef struct
{
    int  (*alloc)(phys_size_t pages, uint8_t flags, alloc_cb cb, void *pv);
    int  (*dealloc)(free_cb cb, void *pv);
}physmm_t;

void     physmm_early_init(void);
int      physmm_init(void);
physmm_t *physmm_get(void);

extern phys_addr_t KERNEL_LMA;
extern phys_addr_t KERNEL_LMA_END;
extern phys_addr_t KERNEL_VMA;
extern phys_addr_t KERNEL_VMA_END;
extern phys_addr_t KERNEL_IMAGE_LEN;
extern phys_addr_t BOOTSTRAP_END;
extern phys_addr_t BOOT_PAGING;
extern phys_addr_t BOOT_PAGING_END;
extern phys_addr_t BOOT_PAGING_LENGTH;
extern phys_addr_t _code;
extern phys_addr_t _code_end;
extern phys_addr_t _data;
extern phys_addr_t _data_end;
extern phys_addr_t _rodata;
extern phys_addr_t _rodata_end;
extern phys_addr_t _bss;
extern phys_addr_t _bss_end;






#endif