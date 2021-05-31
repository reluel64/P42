
#ifndef page_h
#define page_h

#include <stdint.h>

#define PAGE_PRESENT          (1 << 0)
#define PAGE_WRITABLE         (1 << 1)
#define PAGE_USER             (1 << 2)
#define PAGE_WRITE_THROUGH    (1 << 3)
#define PAGE_CACHE_DISABLE    (1 << 4)
#define PAGE_ACCESSED         (1 << 5)
#define PAGE_DIRTY            (1 << 6)
#define PAGE_PAT              (1 << 7)
#define PAGE_GLOBAL           (1 << 8)
#define PAGE_EXECUTE_DIABLE   (1 << 63)

typedef struct _cr3
{
    uint64_t ignored:3;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t ignored_2:7;
    uint64_t page_root:40;
    uint64_t reserved:12;
}__attribute__((packed)) cr3_t;

typedef struct _pml5e
{
    uint64_t present : 1;
    uint64_t read_write:1;
    uint64_t user_supervisor:1;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t accessed:1;
    uint64_t ignored:1;
    uint64_t reserved:1;
    uint64_t ignored_2:4;
    uint64_t pml4t:40; /* Page directory page table address */
    uint64_t ignored_3:11;
    uint64_t xd:1;

}__attribute__((packed)) pml5e_t;

typedef struct _pml4e
{
    uint64_t present : 1;
    uint64_t read_write:1;
    uint64_t user_supervisor:1;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t accessed:1;
    uint64_t ignored:1;
    uint64_t reserved:1;
    uint64_t ignored_2:4;
    uint64_t pdpt:40; /* Page directory page table address */
    uint64_t ignored_3:11;
    uint64_t xd:1;

}__attribute__((packed)) pml4e_t;

typedef struct _pdpte
{
    uint64_t present:1;
    uint64_t read_write:1;
    uint64_t user_supervisor:1;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t accessed:1;
    uint64_t ignored_1:1;     
    uint64_t zero:1; 
    uint64_t ignored_2:4;
    uint64_t pd:40;         /* Page directory address*/
    uint64_t ignored_3:11;
    uint64_t xd:1;
}__attribute__((packed)) pdpte_t;

typedef struct _pde
{
    uint64_t present:1;
    uint64_t read_write:1;
    uint64_t user_supervisor:1;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t accessed:1;
    uint64_t ignored_1:1;     
    uint64_t zero:1; 
    uint64_t ignored_2:4;
    uint64_t pd:40;         /* Page page table address*/
    uint64_t ignored_3:11;
    uint64_t xd:1;
}__attribute__((packed)) pde_t;

typedef struct _pte
{
    uint64_t present:1;
    uint64_t read_write:1;
    uint64_t user_supervisor:1;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t accessed:1;
    uint64_t dirty:1;     
    uint64_t pat:1; 
    uint64_t global:1;
    uint64_t ignored_2:3;
    uint64_t page:40;         /* page frame address*/
    uint64_t ignored_3:11;
    uint64_t xd:1;
}__attribute__((packed)) pte_t;

typedef union cr3_bits
{
    cr3_t fields;
    uint64_t bits;
}__attribute__((packed))  cr3_bits_t;


typedef union pml5_bits
{
    pml5e_t fields;
    uint64_t bits;
}__attribute__((packed))  pml5e_bits_t;

typedef union pml4_bits
{
    pml4e_t fields;
    uint64_t bits;
}__attribute__((packed))  pml4e_bits_t;

typedef union pdpte_bits
{
    pdpte_t fields;
    uint64_t bits;
}__attribute__((packed))  pdpte_bits_t;

typedef union pde_bits
{
    pde_t fields;
    uint64_t bits;
}__attribute__((packed))  pde_bits_t;

typedef union pte_bits
{
    pte_t fields;
    uint64_t bits;
}__attribute__((packed))  pte_bits_t;

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

#define PML5_SHIFT  (48)
#define PML4_SHIFT  (39)
#define PDPT_SHIFT  (30)
#define PDT_SHIFT   (21)
#define PT_SHIFT    (12)

#define PML5_ENTRY_LEN (1 << 48)
#define PML4_ENTRY_LEN (1 << 39)
#define PDPT_ENTRY_LEN (1 << 30)
#define PDT_ENTRY_LEN  (1 << 21)
#define PT_ENTRY_LEN   (1 << 12)

#define VIRT_TO_PML5_INDEX(x)  (((x) >> 48) & 0x1FF)
#define VIRT_TO_PML4_INDEX(x)  (((x) >> 39) & 0x1FF)
#define VIRT_TO_PDPT_INDEX(x)  (((x) >> 30) & 0x1FF)
#define VIRT_TO_PDT_INDEX(x)   (((x) >> 21) & 0x1FF)
#define VIRT_TO_PT_INDEX(x)    (((x) >> 12) & 0x1FF)
#define VIRT_TO_OFFSET(x)      ((x)         & 0x1FFF)

#define PML5_INDEX_TO_VIRT(x)  (((x) & 0x1FF) << 48)
#define PML4_INDEX_TO_VIRT(x)  (((x) & 0x1FF) << 39)
#define PDPT_INDEX_TO_VIRT(x)  (((x) & 0x1FF) << 30)
#define PDT_INDEX_TO_VIRT(x)   (((x) & 0x1FF) << 21)
#define PT_INDEX_TO_VIRT(x)    (((x) & 0x1FF) << 12)
#define OFFSET_TO_VIRT(x)      ((x)  & 0x1FFF)

#define ATTRIBUTE_MASK (0x8000000000000FFF)

#define PAT_MSR               (0x277)

#define PAT_UNCACHEABLE       (0x0)
#define PAT_WRITE_COMBINING   (0x1)
#define PAT_WRITE_THROUGH     (0x4)
#define PAT_WRITE_PROTECTED   (0x5)
#define PAT_WRITE_BACK        (0x6)
#define PAT_UNCACHED          (0x7)


#endif