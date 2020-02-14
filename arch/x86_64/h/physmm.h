#ifndef _physmmh_
#define _physmmh_
#include <stdint.h>
#define PAGE_SIZE (0x1000)

void physmm_init(void);
int physmm_free_pf(uint64_t pf);
uint64_t physmm_alloc_pf(void);

typedef struct
{

}physmm_t;
#endif