#include <stdint.h>
#define MEMORY_USABLE           0x1
#define MEMORY_RESERVED         0x2
#define MEMORY_ACPI_RECLAIMABLE 0x3
#define MEMORY_ACPI_NVS         0x4
#define MEMORY_BAD              0x5

#define MEMORY_ENABLED          0x1


struct memory_map_entry
{
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t extended;
    uint32_t domain;
    uint32_t flags;
};

int mem_map_iter
(
    void (*callback)(struct memory_map_entry *mmap,void *pv),
    void *pv
);