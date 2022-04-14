#ifndef pgmgr_h
#define pgmgr_h

#include <stdint.h>
#include <defs.h>
#include <spinlock.h>
#include <platform.h>
#include <pfmgr.h>

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


#define PGMGR_UPDATE_ENTRIES_THRESHOLD 1024

#define PGMGR_LEVEL_TO_SHIFT(x) (PT_SHIFT + (((x) - 1) << 3) + ((x) - 1))
#define PGMGR_ENTRIES_PER_LEVEL (512)
#define PGMGR_FILL_LEVEL(ld, context, _base,                                   \
                        _length, _req_level, _attr, _cb)                       \
                    do                                                         \
                    {                                                          \
                        (ld)->ctx        = (context);                          \
                                                                               \
                        (ld)->base       = ALIGN_DOWN((_base),                 \
                                           (virt_size_t)1 <<                   \
                                           PGMGR_LEVEL_TO_SHIFT((_req_level)));\
                                                                               \
                        (ld)->level      = NULL;                               \
                        (ld)->length     = (_length) + ((_base)  - (ld)->base);\
                        (ld)->offset     = (0);                                \
                        (ld)->level_phys = (context)->pg_phys;                 \
                        (ld)->req_level  = (_req_level);                       \
                        (ld)->curr_level = (context)->max_level;               \
                        (ld)->error      = 1;                                  \
                        (ld)->do_map     = 1;                                  \
                        (ld)->attr_mask  = (_attr);                            \
                        (ld)->iter_cb    = (_cb);                              \
                        (ld)->cb_status  = 0;                                  \
                    }while(0);
                        
#define PAGE_MASK_ADDRESS(x)                 (((x) & (~(ATTRIBUTE_MASK))))
#define PGMGR_MIN_PAGE_TABLE_LEVEL (0x2)
#define PGMGR_LEVEL_TO_STEP(lvl)            (((virt_size_t)1 << \
                                            PGMGR_LEVEL_TO_SHIFT((lvl))))
#define PGMGR_CLEAR_PT_PAGE(max_level)      ((max_level) + 1)
#define PGMGR_LEVEL_ENTRY_PAGE(max_level)   ((max_level) + 2)

/* Error codes */
#define PGMGR_ERR_OK                  (0)
#define PGMGR_ERR_NO_FRAMES           (1 << 0)
#define PGMGR_ERR_TABLE_NOT_ALLOCATED (1 << 1)
#define PGMGR_ERR_TBL_CREATE_FAIL     (1 << 2)

/* Flags for the iterator callback */
#define PGMGR_CB_LEVEL_GO_DOWN      (1 << 0)
#define PGMGR_CB_LEVEL_GO_UP        (1 << 1)
#define PGMGR_CB_NEXT_ENTRY         (1 << 2)
#define PGMGR_CB_DO_REQUEST         (1 << 3)
#define PGMGR_CB_RES_CHECK          (1 << 4)

/* Return codes for the iter callbacks */

#define PGMGR_CB_STOP               (1 << 0)
#define PGMGR_CB_ERROR              (1 << 1)
#define PGMGR_CB_BREAK              (1 << 2)
#define PGMGR_CB_FORCE_GO_UP        (1 << 3)

#define PGMGR_MAX_TABLE_INDEX       (0x1FF)
typedef struct pgmgr_ctx_t
{
     phys_addr_t pg_phys; /* physical location of the first
                           * level of paging
                           */ 
    spinlock_t  lock;
    uint8_t max_level;     /* paging level */
}pgmgr_ctx_t;


/* forward declarations */
typedef struct pgmgr_iter_callback_data_t pgmgr_iter_callback_data_t;
typedef struct pgmgr_level_data_t         pgmgr_level_data_t;

typedef struct pgmgr_contig_find_t
{
    phys_addr_t base;
    phys_size_t pf_count;
    phys_size_t pf_req;
}pgmgr_contig_find_t;

typedef struct pgmgr_level_data_t
{
    pgmgr_ctx_t *ctx;
    virt_addr_t base;
    virt_addr_t *level;
    virt_size_t length;
    virt_size_t offset;
    phys_addr_t level_phys;
    uint8_t     req_level;
    uint8_t     curr_level;
    uint8_t     error;
    uint8_t     do_map;
    phys_size_t attr_mask;
    uint32_t    cb_status;
    void        (*iter_cb)
    (
        pgmgr_iter_callback_data_t *ic, 
        pgmgr_level_data_t *ld,
        pfmgr_cb_data_t *pfmgr_dat,
        uint32_t op
    );
}pgmgr_level_data_t;

typedef struct pgmgr_iter_callback_data_t
{
    /* Status for the iter callback */
    virt_addr_t         vaddr;
    uint16_t            entry;
    uint8_t             shift;
    virt_addr_t         next_vaddr;
    virt_size_t         increment;
}pgmgr_iter_callback_data_t;



int         pgmgr_init(pgmgr_ctx_t *ctx);
void        pgmgr_boot_temp_map_init(void);
int         pgmgr_install_handler(void);
uint64_t    page_manager_get_base(void);


uint8_t     pgmgr_nx_support(void);
uint8_t     pgmgr_pml5_support(void);
int         pgmgr_per_cpu_init(void);

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

int pgmgr_allocate_backend
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t *out_len
);

int pgmgr_release_backend
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len
);

int pgmgr_allocate_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len,
    uint32_t    vm_attr
);

int pgmgr_release_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len
);

int pgmgr_map_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len,
    uint32_t    vm_attr,
    phys_addr_t phys
);

int pgmgr_unmap_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t *out_len
);

void pgmgr_invalidate
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t len
);

#endif