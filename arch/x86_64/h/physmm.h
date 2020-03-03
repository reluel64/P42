#ifndef physmm_h
#define physmm_h
#include <stdint.h>

#define PAGE_SIZE (0x1000)

#define ALLOC_CONTIG (1<<0)
#define ALLOC_HIGHEST (1<<2)

typedef void (*alloc_cb)(uint64_t phys);

void physmm_early_init(void);
void physmm_init(void);
int physmm_early_free_pf(uint64_t pf);
uint64_t physmm_early_alloc_pf(void);
int physmm_alloc_pf(uint64_t length, uint8_t flags, alloc_cb cb);





#endif