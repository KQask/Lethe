#include "../Globals.h"
#include "Npt.h"
#include "../util.h"


// Everything here that allocates does so at init time (PASSIVE_LEVEL). The
// VMEXIT path runs at GIF=0; calling ExAllocatePool2 from there cross-IPIs
// and deadlocks (bugcheck 0x101). So we preallocate a big table pool up
// front and the runtime path just bumps a counter.

static void npt_pava_insert(__npt_global_t* g, uint64_t pa, __npt_table_t* va);

// Slicing 2MB chunks into 4KB sub-tables. Doing 65K individual ExAllocatePool2
// calls took 4-5 minutes at boot; this gets it down to seconds.
static uint32_t npt_global_prealloc_chunk( __npt_global_t* g )
{
    if (g->chunk_count >= NPT_TABLE_CHUNK_MAX) {
        log_error("[NptGlobal] prealloc_chunk: chunk_count cap (%u) reached", g->chunk_count);
        return 0;
    }
    if (g->alloc_count + NPT_TABLE_CHUNK_TABLES > NPT_MAX_TABLE_ALLOCS) {
        log_error("[NptGlobal] prealloc_chunk: alloc cap (%u + %u > %u)",
                  g->alloc_count, (uint32_t)NPT_TABLE_CHUNK_TABLES, (uint32_t)NPT_MAX_TABLE_ALLOCS);
        return 0;
    }

    uint8_t* chunk = (uint8_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, NPT_TABLE_CHUNK_BYTES, POOLTAG);
    if (!chunk) {
        log_error("[NptGlobal] prealloc_chunk: ExAllocatePool2(2MB) failed at chunk %u (already allocated %u tables)",
                  g->chunk_count, g->alloc_count);
        return 0;
    }

    g->chunks[g->chunk_count++] = chunk;

    // Virtual contiguity of the chunk doesn't imply physical contiguity,
    // each sub-table gets its own PA.
    for (uint32_t i = 0; i < NPT_TABLE_CHUNK_TABLES; i++) {
        __npt_alloc_t* slot = &g->allocs[g->alloc_count++];
        slot->va = (__npt_table_t*)(chunk + i * sizeof(__npt_table_t));
        slot->pa = MmGetPhysicalAddress(slot->va).QuadPart;
        npt_pava_insert(g, slot->pa & ~0xFFFULL, slot->va);
    }
    return NPT_TABLE_CHUNK_TABLES;
}

static __npt_alloc_t* npt_global_alloc_table( __npt_global_t* g )
{
    if (g->alloc_count >= g->alloc_capacity) {
        log_error("[NptGlobal] runtime pool exhausted (count=%u capacity=%u)",
                  g->alloc_count, g->alloc_capacity);
        return nullptr;
    }
    return &g->allocs[g->alloc_count++];
}

static inline uint32_t npt_pava_hash(uint64_t pa)
{
    uint64_t k = pa >> 12;
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return (uint32_t)k & (NPT_PAVA_HASH_SIZE - 1);
}

static void npt_pava_insert(__npt_global_t* g, uint64_t pa, __npt_table_t* va)
{
    uint32_t i = npt_pava_hash(pa);
    while (g->pava_table[i].pa != 0) {
        i = (i + 1) & (NPT_PAVA_HASH_SIZE - 1);
    }
    g->pava_table[i].pa = pa;
    g->pava_table[i].va = va;
}

static __npt_table_t* npt_global_va_from_pa( __npt_global_t* g, uint64_t pa )
{
    pa &= ~0xFFFULL;
    uint32_t i = npt_pava_hash(pa);
    while (g->pava_table[i].pa != 0) {
        if (g->pava_table[i].pa == pa) return g->pava_table[i].va;
        i = (i + 1) & (NPT_PAVA_HASH_SIZE - 1);
    }
    return nullptr;
}

// Intermediate tables are RWX; leaf flags carry the actual permission.
// 4KB-everywhere identity; no splits.
static npt_entry_t* npt_global_assign_entry_4k( __npt_global_t* g,
                                                __npt_table_t* pml4_root,
                                                uint64_t       gpa,
                                                uint64_t       leaf_flags )
{
    const uint64_t pml4_i = NPT_PML4_INDEX(gpa);
    const uint64_t pdpt_i = NPT_PDPT_INDEX(gpa);
    const uint64_t pd_i   = NPT_PD_INDEX(gpa);
    const uint64_t pt_i   = NPT_PT_INDEX(gpa);

    // PML4 -> PDPT
    npt_entry_t* pml4e = &pml4_root->entries[pml4_i];
    __npt_table_t* pdpt;
    if (!(*pml4e & NPT_PRESENT)) {
        __npt_alloc_t* alloc = npt_global_alloc_table(g);
        if (!alloc) return nullptr;
        *pml4e = NPT_PHYS_TO_ENTRY(alloc->pa) | NPT_FLAGS_RWX;
        pdpt = alloc->va;
    } else {
        pdpt = npt_global_va_from_pa(g, NPT_ENTRY_TO_PHYS(*pml4e));
        if (!pdpt) return nullptr;
    }

    // PDPT -> PD
    npt_entry_t* pdpte = &pdpt->entries[pdpt_i];
    __npt_table_t* pd;
    if (!(*pdpte & NPT_PRESENT)) {
        __npt_alloc_t* alloc = npt_global_alloc_table(g);
        if (!alloc) return nullptr;
        *pdpte = NPT_PHYS_TO_ENTRY(alloc->pa) | NPT_FLAGS_RWX;
        pd = alloc->va;
    } else {
        pd = npt_global_va_from_pa(g, NPT_ENTRY_TO_PHYS(*pdpte));
        if (!pd) return nullptr;
    }

    // PD -> PT
    npt_entry_t* pde = &pd->entries[pd_i];
    __npt_table_t* pt;
    if (!(*pde & NPT_PRESENT)) {
        __npt_alloc_t* alloc = npt_global_alloc_table(g);
        if (!alloc) return nullptr;
        *pde = NPT_PHYS_TO_ENTRY(alloc->pa) | NPT_FLAGS_RWX;
        pt = alloc->va;
    } else {
        pt = npt_global_va_from_pa(g, NPT_ENTRY_TO_PHYS(*pde));
        if (!pt) return nullptr;
    }

    // 4KB leaf: identity-mapped with caller-supplied flags.
    npt_entry_t* pte = &pt->entries[pt_i];
    *pte = NPT_PHYS_TO_ENTRY(gpa & ~0xFFFULL) | leaf_flags;
    return pte;
}

// 4KB everywhere for every PA in MmGetPhysicalMemoryRanges, plus the APIC
// BAR page. I tried 1GB-with-on-demand-split first; cumulative coherency
// failures at split boundaries took two weekends to chase. ~128MB extra
// pool to make that bug class go away.
static NTSTATUS npt_global_build_identity( __npt_global_t* g,
                                           __npt_table_t** out_pml4,
                                           uint64_t*       out_pml4_pa )
{
    __npt_alloc_t* root = npt_global_alloc_table(g);
    if (!root) return STATUS_NO_MEMORY;
    *out_pml4 = root->va;
    *out_pml4_pa = root->pa;

    // Iterate physical memory ranges. Only map RAM that the OS actually
    // reports; gaps (MMIO, reserved firmware) are left unmapped and
    // handled on-demand by handle_npf if the guest touches them.
    PPHYSICAL_MEMORY_RANGE ranges = MmGetPhysicalMemoryRanges();
    PPHYSICAL_MEMORY_RANGE original = ranges;
    if (!ranges) return STATUS_UNSUCCESSFUL;

    while (ranges->NumberOfBytes.QuadPart != 0) {
        const uint64_t base = (uint64_t)ranges->BaseAddress.QuadPart;
        const uint64_t end  = base + (uint64_t)ranges->NumberOfBytes.QuadPart;
        for (uint64_t pa = base; pa < end; pa += NPT_PAGE_SIZE) {
            if (!npt_global_assign_entry_4k(g, root->va, pa, NPT_FLAGS_RWX)) {
                ExFreePool(original);
                return STATUS_NO_MEMORY;
            }
        }
        ranges++;
    }
    ExFreePool(original);

    // APIC BAR (MSR 0x1B). Bits 12-51 hold the APIC physical base. The APIC
    // region is required for guest interrupt delivery and isn't covered by
    // MmGetPhysicalMemoryRanges, so map it explicitly.
    const uint64_t apic_msr = __readmsr(0x1B);
    const uint64_t apic_pa  = apic_msr & 0x000FFFFFFFFFF000ULL;
    if (apic_pa) {
        (void)npt_global_assign_entry_4k(g, root->va, apic_pa, NPT_FLAGS_RWX);
    }

    return STATUS_SUCCESS;
}

NTSTATUS npt_global_init( __vmm_context_t* ctx )
{
    if (ctx->npt_global) return STATUS_SUCCESS;

    __npt_global_t* g = (__npt_global_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(__npt_global_t), POOLTAG);
    if (!g) return STATUS_NO_MEMORY;

    g->allocs = (__npt_alloc_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(__npt_alloc_t) * NPT_MAX_TABLE_ALLOCS, POOLTAG);
    if (!g->allocs) {
        ExFreePoolWithTag(g, POOLTAG);
        return STATUS_NO_MEMORY;
    }

    g->pava_table = (__pava_entry_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(__pava_entry_t) * NPT_PAVA_HASH_SIZE, POOLTAG);
    if (!g->pava_table) {
        ExFreePoolWithTag(g->allocs, POOLTAG);
        ExFreePoolWithTag(g, POOLTAG);
        return STATUS_NO_MEMORY;
    }

    // Bulk-prealloc the table pool in 2MB chunks. See comment on
    // npt_global_prealloc_chunk for why.
    while (g->alloc_count < NPT_MAX_TABLE_ALLOCS) {
        if (npt_global_prealloc_chunk(g) == 0) {
            log_error("[NptGlobal] prealloc died at chunk %u / %u tables",
                      g->chunk_count, g->alloc_count);
            for (uint32_t c = 0; c < g->chunk_count; c++) {
                if (g->chunks[c]) ExFreePoolWithTag(g->chunks[c], POOLTAG);
            }
            if (g->pava_table) ExFreePoolWithTag(g->pava_table, POOLTAG);
            if (g->allocs)     ExFreePoolWithTag(g->allocs,     POOLTAG);
            ExFreePoolWithTag(g, POOLTAG);
            return STATUS_NO_MEMORY;
        }
    }
    g->alloc_capacity = g->alloc_count;
    g->alloc_count    = 0;

    NTSTATUS s = npt_global_build_identity(g, &g->pml4, &g->pml4_pa);
    if (!NT_SUCCESS(s)) {
        ctx->npt_global = g;
        npt_global_destroy(ctx);
        return s;
    }

    ctx->npt_global = g;

    log_debug("[NptGlobal] built: used %u/%u tables, pml4_pa=0x%llX",
              g->alloc_count, g->alloc_capacity, g->pml4_pa);
    return STATUS_SUCCESS;
}


NTSTATUS npt_attach_global( __vcpu_t* vcpu )
{
    if (!vcpu || !vcpu->vmm_context || !vcpu->vmm_context->npt_global)
        return STATUS_DEVICE_NOT_READY;

    __npt_global_t* g = (__npt_global_t*)vcpu->vmm_context->npt_global;

    vcpu->vmcb->control.np_enable   = SVM_NP_ENABLE;
    vcpu->vmcb->control.n_cr3       = g->pml4_pa;
    vcpu->vmcb->control.vmcb_clean &= ~VMCB_CLEAN_NP;
    return STATUS_SUCCESS;
}


void npt_global_destroy( __vmm_context_t* ctx )
{
    if (!ctx || !ctx->npt_global) return;
    __npt_global_t* g = (__npt_global_t*)ctx->npt_global;

    // Free the chunks (each is one ExAllocatePool2 of NPT_TABLE_CHUNK_BYTES).
    // The allocs[] entries are slices INTO those chunks; do NOT free
    // individual allocs[i].va.
    for (uint32_t c = 0; c < g->chunk_count; c++) {
        if (g->chunks[c]) ExFreePoolWithTag(g->chunks[c], POOLTAG);
    }
    if (g->pava_table) ExFreePoolWithTag(g->pava_table, POOLTAG);
    if (g->allocs)     ExFreePoolWithTag(g->allocs,     POOLTAG);
    ExFreePoolWithTag(g, POOLTAG);
    ctx->npt_global = nullptr;
}


void handle_db(__vcpu_t* vcpu) {
    // We never set TF ourselves, so any #DB we catch belongs to the guest
    // (kernel debugger, user-mode debugger, whatever). Re-inject it.
    __svm_event_inject_t ev = {};
    ev.bits.vector = EXVEC_DB;
    ev.bits.type = 3;
    ev.bits.valid = 1;
    vcpu->vmcb->control.event_inject = ev.flags;
}


void handle_npf( __vcpu_t* vcpu )
{
    uint64_t errCode = vcpu->vmcb->control.exit_info_1;
    uint64_t gpa     = vcpu->vmcb->control.exit_info_2;

    __npt_global_t* g = (__npt_global_t*)vcpu->vmm_context->npt_global;
    if (!g)
        return;

    const uint64_t page_gpa = gpa & ~(NPT_PAGE_SIZE - 1);

    vcpu->vmcb->control.vmcb_clean &= ~VMCB_CLEAN_NP;
    vcpu->vmcb->control.tlb_control = 1;

    const bool present = (errCode & NPF_PRESENT) != 0;
    if (!present) {
        // Lazy install for any range not in the initial identity build
        // (MMIO, holes the OS didn't report). Identity-map with RWX.
        (void)npt_global_assign_entry_4k(g, g->pml4, page_gpa, NPT_FLAGS_RWX);
    }
}
