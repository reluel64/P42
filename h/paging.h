
#ifndef page_h
#define page_h

#include <stdint.h>

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

#define VIRT_TO_PML5_INDEX(x)  (((x) >> 48) & 0x1FF)
#define VIRT_TO_PML4_INDEX(x)  (((x) >> 39) & 0x1FF)
#define VIRT_TO_PDPT_INDEX(x) (((x) >> 30) & 0x1FF)
#define VIRT_TO_PDT_INDEX(x)   (((x) >> 21) & 0x1FF)
#define VIRT_TO_PT_INDEX(x)   (((x) >> 12) & 0x1FF)
#define VIRT_TO_OFFSET(x)      ((x)         & 0x1FFF)

#define PML5_INDEX_TO_VIRT(x)  (((x) & 0x1FF) << 47)
#define PML4_INDEX_TO_VIRT(x)  (((x) & 0x1FF) << 39 )
#define PDPT_INDEX_TO_VIRT(x) (((x) & 0x1FF) << 30 )
#define PDT_INDEX_TO_VIRT(x)   (((x) & 0x1FF) << 21)
#define PT_INDEX_TO_VIRT(x)   (((x) & 0x1FF) << 12)
#define OFFSET_TO_VIRT(x)      ((x)  & 0x1FFF)

#define ATTRIBUTE_MASK (0x8000000000000FFF)

#endif