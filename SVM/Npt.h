#pragma once
#include "Arch/Npt.h"

struct __vcpu_t;

// 65536 * 4KB tables = ~256MB non-paged pool, scales linearly with RAM size.
#define NPT_MAX_TABLE_ALLOCS    65536

// 2x alloc count -> ~50% load. Direct-mapped hash with linear probing.
// MmGetVirtualForPhysical doesn't reliably work for our pool memory.
#define NPT_PAVA_HASH_SIZE  (NPT_MAX_TABLE_ALLOCS * 2)

struct __pava_entry_t
{
    uint64_t        pa;             // 4KB-aligned PA. 0 = empty slot.
    __npt_table_t*  va;
};

struct __npt_alloc_t
{
    __npt_table_t*  va;     // kernel virtual address
    uint64_t        pa;     // physical address
};

// Bulk-allocation tuning. ~65K tables of 4KB each. Allocating one at a time
// takes minutes at boot due to per-call lock + heap-search overhead, so we
// allocate in 2MB chunks and slice each chunk into 4KB sub-tables.
#define NPT_TABLE_CHUNK_BYTES   (2 * 1024 * 1024)
#define NPT_TABLE_CHUNK_TABLES  (NPT_TABLE_CHUNK_BYTES / 4096)
#define NPT_TABLE_CHUNK_MAX     (NPT_MAX_TABLE_ALLOCS / NPT_TABLE_CHUNK_TABLES + 4)

struct __npt_global_t
{
    __npt_table_t*  pml4;
    uint64_t        pml4_pa;

    // Table allocator. alloc_count is bumped lazily by handle_npf for any
    // physical range not covered by the initial identity build.
    __npt_alloc_t*  allocs;
    uint32_t        alloc_count;
    uint32_t        alloc_capacity;

    // Backing chunks (one ExAllocatePool2 each, sliced into 4KB tables).
    // On destroy we free chunks, NOT allocs[].
    void*           chunks[NPT_TABLE_CHUNK_MAX];
    uint32_t        chunk_count;

    // PA->VA hash table for resolving table PAs back to their VAs.
    __pava_entry_t* pava_table;
};

NTSTATUS npt_global_init( struct __vmm_context_t* ctx );
void     npt_global_destroy( struct __vmm_context_t* ctx );

NTSTATUS npt_attach_global( __vcpu_t* vcpu );

// AMD APM Vol. 2, Section 15.25.6. exit_info_1=error code, exit_info_2=GPA.
#define NPF_PRESENT          ( 1ULL <<  0 )
#define NPF_WRITE            ( 1ULL <<  1 )
#define NPF_USER_MODE        ( 1ULL <<  2 )
#define NPF_RESERVED         ( 1ULL <<  3 )
#define NPF_EXECUTE          ( 1ULL <<  4 )
#define NPF_NESTED_PAGE_WALK ( 1ULL << 32 )
#define NPF_GUEST_PAGE_WALK  ( 1ULL << 33 )

#define VMCB_CLEAN_NP   ( 1u << 4 )     // NP_ENABLE + N_CR3

void handle_db( __vcpu_t* vcpu );
void handle_npf( __vcpu_t* vcpu );
