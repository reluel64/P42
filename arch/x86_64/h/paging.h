
#ifndef page_h
#define page_h

#include <stdint.h>

/* Page flags */

#define PAGE_PRESENT          (1ull << 0)
#define PAGE_WRITABLE         (1ull << 1)
#define PAGE_USER             (1ull << 2)
#define PAGE_WRITE_THROUGH    (1ull << 3)
#define PAGE_CACHE_DISABLE    (1ull << 4)
#define PAGE_ACCESSED         (1ull << 5)
#define PAGE_DIRTY            (1ull << 6)
#define PAGE_PAT              (1ull << 7)
#define PAGE_TABLE_SIZE       (1ull << 7)
#define PAGE_GLOBAL           (1ull << 8)
#define PAGE_EXECUTE_DISABLE  (1ull << 63)

typedef struct pat_bits_t
{
    uint32_t pa0:3;
    uint32_t rsrvd0:5;
    uint32_t pa1:3;
    uint32_t rsrvd1:5;
    uint32_t pa2:3;
    uint32_t rsrvd2:5;
    uint32_t pa3:3;
    uint32_t rsrvd3:5;
    uint32_t pa4:3;
    uint32_t rsrvd4:5;
    uint32_t pa5:3;
    uint32_t rsrvd5:5;
    uint32_t pa6:3;
    uint32_t rsrvd6:5;
    uint32_t pa7:3;
    uint32_t rsrvd7:5;
    
}__attribute__ ((packed)) pat_bits_t;

typedef union pat_t
{
    uint64_t pat;
    pat_bits_t fields;
}__attribute__ ((packed)) pat_t;

#define PT_SHIFT    (12)

#define ATTRIBUTE_MASK (0x8000000000000FFF)

#define PAT_MSR               (0x277)

#define PAT_UNCACHEABLE       (0x0)
#define PAT_WRITE_COMBINING   (0x1)
#define PAT_WRITE_THROUGH     (0x4)
#define PAT_WRITE_PROTECTED   (0x5)
#define PAT_WRITE_BACK        (0x6)
#define PAT_UNCACHED          (0x7)


#endif