/* Platform-dependent memory management */
#include <stdint.h>

typedef struct _cr3
{
    uint64_t ignored:3;
    uint64_t write_through:1;
    uint64_t cache_disable:1;
    uint64_t ignored_2:7;
    uint64_t pml4:40;
    uint64_t reserved:12;
}cr3_t;

typedef struct _pml4
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

}pml4_t;

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
}pdpte_t;

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
}pde_t;

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
}pte_t;

typedef union cr3_bits
{
    cr3_t fields;
    uint64_t bits;
}cr3_bits_t;

typedef union pml4_bits
{
    pml4_t fields;
    uint64_t bits;
}pml4_bits_t;

typedef union pdpte_bits
{
    pdpte_t fields;
    uint64_t bits;
}pdpte_bits_t;

typedef union pde_bits
{
    pde_t fields;
    uint64_t bits;
}pde_bits_t;

typedef union pte_bits
{
    pte_t fields;
    uint64_t bits;
}pte_bits_t;

void init_page_table(void)
{

}