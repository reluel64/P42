#ifndef physmm_h
#define physmm_h
#include <stdint.h>
#define PAGE_SIZE (0x1000)



void physmm_early_init(void);
void physmm_init(void);
int physmm_early_free_pf(uint64_t pf);
uint64_t physmm_early_alloc_pf(void);
int physmm_alloc_pf()





#endif