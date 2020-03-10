#ifndef physmm_h
#define physmm_h
#include <stdint.h>

#define PAGE_SIZE (0x1000)


#define LOW_MEMORY   (0x100000)
#define ALLOC_CONTIG (1<<0)
#define ALLOC_HIGHEST (1<<2)
#define ALLOC_ISA_DMA     (1<<3)

typedef void (*alloc_cb)(uint64_t phys, uint64_t length, void *pv);

void     physmm_early_init(void);
int      physmm_init(void);
int      physmm_early_free_pf(uint64_t pf);
uint64_t physmm_early_alloc_pf(void);
int      physmm_alloc_pf(uint64_t length, uint8_t flags, alloc_cb cb, void *pv);

#endif