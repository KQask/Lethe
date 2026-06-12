#pragma once
#include <ntddk.h>
#include <cstdint>

// NPT hardware entry layout. AMD APM Vol. 2, Section 15.25.

#define NPT_PRESENT         ( 1ULL <<  0 )  // Entry is valid; CPU will use it
#define NPT_WRITABLE        ( 1ULL <<  1 )  // 1 = writable, 0 = read-only
#define NPT_USER_ACCESSIBLE ( 1ULL <<  2 )  // 1 = user+supervisor, 0 = supervisor only
#define NPT_WRITE_THROUGH   ( 1ULL <<  3 )  // Page Write-Through: cache policy, match host MTRRs
#define NPT_CACHE_DISABLE   ( 1ULL <<  4 )  // Disable caching for this page (use for MMIO/device memory)
#define NPT_ACCESSED        ( 1ULL <<  5 )  // Set by hardware on any access
#define NPT_DIRTY           ( 1ULL <<  6 )  // Set by hardware on write (PTE only)
#define NPT_LARGE_PAGE      ( 1ULL <<  7 )  // This entry IS the page, not a pointer to the next table.
#define NPT_GLOBAL          ( 1ULL <<  8 )  // TLB entry not flushed on CR3 reload (PTE only)
#define NPT_NO_EXECUTE      ( 1ULL << 63 )  // Prevents instruction fetches (requires EFER.NXE=1)

// View flags used by handle_npf to swap between hook and original pages.
#define NPT_FLAGS_EXEC_ONLY     ( NPT_PRESENT | NPT_USER_ACCESSIBLE )
#define NPT_FLAGS_RW_NOEXEC     ( NPT_PRESENT | NPT_WRITABLE | NPT_USER_ACCESSIBLE | NPT_NO_EXECUTE )

#define NPT_FLAGS_RWX           ( NPT_PRESENT | NPT_WRITABLE | NPT_USER_ACCESSIBLE )

#define NPT_FLAGS_UNCACHED      ( NPT_PRESENT | NPT_WRITABLE | NPT_USER_ACCESSIBLE | NPT_CACHE_DISABLE | NPT_WRITE_THROUGH )

#define NPT_PHYS_ADDR_MASK              0x000FFFFFFFFFF000ULL
#define NPT_PHYS_TO_ENTRY( pa )   (  (pa)  & NPT_PHYS_ADDR_MASK )  // place a physical addr into an entry
#define NPT_ENTRY_TO_PHYS( e  )   (  (e)   & NPT_PHYS_ADDR_MASK )  // extract the physical addr from an entry

#define NPT_PML4_INDEX( gpa )     ( ( (uint64_t)(gpa) >> 39 ) & 0x1FFULL )
#define NPT_PDPT_INDEX( gpa )     ( ( (uint64_t)(gpa) >> 30 ) & 0x1FFULL )
#define NPT_PD_INDEX(   gpa )     ( ( (uint64_t)(gpa) >> 21 ) & 0x1FFULL )
#define NPT_PT_INDEX(   gpa )     ( ( (uint64_t)(gpa) >> 12 ) & 0x1FFULL )

#define NPT_ENTRIES_PER_TABLE   512
#define NPT_PAGE_SIZE           0x1000ULL
#define NPT_2MB_SIZE            0x200000ULL
#define NPT_1GB_SIZE            0x40000000ULL

#define NPT_ALIGN_1GB( a )      ( (uint64_t)(a) & ~( NPT_1GB_SIZE - 1 ) )
#define NPT_ALIGN_2MB( a )      ( (uint64_t)(a) & ~( NPT_2MB_SIZE - 1 ) )

// All 4 levels share this layout.
typedef uint64_t npt_entry_t;

#pragma pack(push, 1)
union __npt_entry_t
{
    uint64_t raw;
    struct
    {
        uint64_t present         :  1;  // Present
        uint64_t writable        :  1;  // Read/Write
        uint64_t user_accessible :  1;  // User/Supervisor (see NPT_USER_ACCESSIBLE note above)
        uint64_t write_through   :  1;  // Page Write-Through
        uint64_t cache_disable   :  1;  // Page Cache Disable
        uint64_t accessed        :  1;  // Accessed (set by hardware)
        uint64_t dirty           :  1;  // Dirty    (set by hardware, PTE only)
        uint64_t large_page      :  1;  // Large page / PAT
        uint64_t global          :  1;  // Global   (PTE only)
        uint64_t avl             :  3;  // Available for software use
        uint64_t pfn             : 40;  // Physical Frame Number (PA >> 12)
        uint64_t avl2            : 11;  // Available for software use
        uint64_t no_execute      :  1;  // No Execute
    } bits;
};
static_assert( sizeof( __npt_entry_t ) == 8, "NPT entry must be 8 bytes" );
#pragma pack(pop)

#pragma pack(push, 1)
struct __npt_table_t
{
    DECLSPEC_ALIGN( 0x1000 ) npt_entry_t entries[ NPT_ENTRIES_PER_TABLE ];
};
static_assert( sizeof( __npt_table_t ) == 0x1000, "NPT table must be exactly 4KB" );
#pragma pack(pop)
