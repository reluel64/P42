#ifndef pgmgr_h
#define pgmgr_h

#include <stdint.h>
#include <defs.h>
#include <spinlock.h>
#include <platform.h>

#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)
#define REMAP_TABLE_SIZE  (0x200000)
#define BOOT_REMAP_TABLE_VADDR (0xFFFFFFFFBFE00000)

#define TEMP_MAP_PFMGR_START (510)
#define TEMP_MAP_PFMGR_END   (511)


#define TEMP_MAP_ACPI_START (508)
#define TEMP_MAP_ACPI_END   (509)


#define PGMGR_WRITABLE        (1 << 0)
#define PGMGR_USER            (1 << 1)
#define PGMGR_EXECUTABLE      (1 << 4)
#define PGMGR_GUARD           (1 << 5)
#define PGMGR_STRONG_UNCACHED (1 << 6)
#define PGMGR_UNCACHEABLE     (1 << 7)
#define PGMGR_WRITE_COMBINE   (1 << 8)
#define PGMGR_WRITE_THROUGH   (1 << 9)
#define PGMGR_WRITE_BACK      (1 << 10)
#define PGMGR_WRITE_PROTECT   (1 << 11)


typedef struct pgmgr_ctx_t
{
     phys_addr_t pg_phys; /* physical location of the first
                           * level of paging
                           */ 
    spinlock_t  lock;
    uint8_t max_level;     /* paging level */
}pgmgr_ctx_t;


int         pgmgr_init(pgmgr_ctx_t *ctx);
void        pgmgr_boot_temp_map_init(void);
int         pgmgr_install_handler(void);
uint64_t    page_manager_get_base(void);


uint8_t     pgmgr_nx_support(void);
uint8_t     pgmgr_pml5_support(void);
int         pgmgr_per_cpu_init(void);


int pgmgr_alloc
(
    pgmgr_ctx_t *ctx,
    virt_addr_t    virt,
    virt_size_t    length,
    uint32_t       attr
);

int pgmgr_map
(
    pgmgr_ctx_t *ctx,
    virt_addr_t    virt,
    virt_size_t    length,
    phys_addr_t    phys, 
    uint32_t       attr
);

int pgmgr_temp_unmap
(
    virt_addr_t vaddr
);

virt_addr_t pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
);

int pgmgr_change_attrib
(
    pgmgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len, 
    uint32_t attr
);

int pgmgr_free
(
    pgmgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
);

int pgmgr_unmap
(
    pgmgr_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
);

#endif