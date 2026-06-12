#pragma once

#define POOLTAG         'MmPg'      // blends with Memory Manager page allocations
#define VMM_STACK_SIZE  0x8000

// -----------------------------------------------------------------------
// Intercept-bit helpers for __vmcb_control_area_t::intercept_misc_1/2
// Reference: AMD64 APM Vol.2 §15.9–15.14
// -----------------------------------------------------------------------


#define CPUID_FN8000_0001_ECX_SVM                                   (1UL << 2)
#define CPUID_FN0000_0001_ECX_HYPERVISOR_PRESENT                    (1UL << 31)
#define CPUID_FN8000_000A_EDX_NP                                    (1UL << 0)

#define CPUID_MAX_STANDARD_FN_NUMBER_AND_VENDOR_STRING              0x00000000
#define CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS           0x00000001
#define CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS_EX        0x80000001
#define CPUID_SVM_FEATURES                                          0x8000000a
#define CPUID_SVM_FEATURES_EX                                       0x80000001

#define SVM_MSR_VM_CR                                               0xc0010114
#define SVM_MSR_VM_HSAVE_PA                                         0xc0010117

#define SVM_VM_CR_SVMDIS                                            (1UL << 4)

#define SVM_MSR_PERMISSIONS_MAP_SIZE                                (PAGE_SIZE * 2)

#define SVM_INTERCEPT_CPUID     (1u << 18)
#define SVM_INTERCEPT_INVD      (1u << 22)
#define SVM_INTERCEPT_HLT       (1u << 24)
#define SVM_INTERCEPT_MSR       (1u << 28)

#define SVM_VINTR_MASKING       (1u << 24) // physical interrupts use host IF, not guest IF

// Always set VMRUN so a guest cannot launch a nested hypervisor.
#define SVM_INTERCEPT_VMRUN     (1u << 0)   // exit 0x080
#define SVM_INTERCEPT_VMMCALL   (1u << 1)   // exit 0x081
#define SVM_INTERCEPT_VMLOAD    (1u << 2)   // exit 0x082
#define SVM_INTERCEPT_VMSAVE    (1u << 3)   // exit 0x083
#define SVM_INTERCEPT_STGI      (1u << 4)   // exit 0x084
#define SVM_INTERCEPT_CLGI      (1u << 5)   // exit 0x085
#define SVM_INTERCEPT_SKINIT    (1u << 6)
#define SVM_INTERCEPT_WBINVD    (1u << 9)

// np_enable (offset 0x090)
#define SVM_NP_ENABLE           (1ull << 0) // enable nested paging (NPT)

// -----------------------------------------------------------------------
// VMCB Control Area  (bytes 0x000–0x3FF of the 4 KB VMCB page)
// Reference: AMD64 APM Vol.2 Appendix B, Table B-1
//
// -----------------------------------------------------------------------
#pragma pack(push, 1)
struct __vmcb_control_area_t // (We have to ensure everything is alligned perfectly to 4kb)
{
    uint16_t intercept_cr_read;
    uint16_t intercept_cr_write;
    uint16_t intercept_dr_read;         // 0x008
    uint16_t intercept_dr_write;        // 0x00C
    uint32_t intercept_exceptions;
    uint32_t intercept_misc_1;
    uint32_t intercept_misc_2;

    uint8_t  reserved_0[0x28];          // 0x01C

    uint16_t pause_filter_threshold;    // 0x03C
    uint16_t pause_filter_count;        // 0x03E

    uint64_t iopm_base_pa;
    uint64_t msrpm_base_pa;
    uint64_t tsc_offset;                // 0x050

    uint32_t guest_asid;
    uint32_t tlb_control;

    uint64_t vintr;
    uint64_t interrupt_shadow;          // 0x068

    // ---- exit information (read-only, written by CPU on #VMEXIT) ----
    uint64_t exit_code;
    uint64_t exit_info_1;
    uint64_t exit_info_2;
    uint64_t exit_int_info;

    uint64_t np_enable;

    uint8_t  reserved_1[0x10];          // 0x098

    uint64_t event_inject;
    uint64_t n_cr3;
    uint64_t lbr_virtualization;        // 0x0B8

    uint64_t vmcb_clean;
    uint64_t next_rip;

    uint8_t  instruction_bytes[16];

    uint8_t  reserved_2[0x320];
};
static_assert(sizeof(__vmcb_control_area_t) == 0x400, "__vmcb_control_area_t must be 0x400 bytes");
#pragma pack(pop)

// -----------------------------------------------------------------------
// Allocate with MmAllocateContiguousMemory and RtlSecureZeroMemory.
// -----------------------------------------------------------------------
#pragma pack(push, 1)
struct __vmcb_t
{
    __vmcb_control_area_t control;  // bytes 0x000–0x3FF
    __vmcb_save_state_t   save;     // bytes 0x400–0xFFF
};
#pragma pack(pop)
static_assert(sizeof(__vmcb_t) == 0x1000, "__vmcb_t must be exactly 4 KB");

// -----------------------------------------------------------------------
// Layout MUST match the push/pop order in Entrypoint.asm exactly.
//
// -----------------------------------------------------------------------
struct __guest_registers_t
{
    uint64_t rbx;   // offset 0   ← last pushed, RSP points here
    uint64_t rcx;   // offset 8
    uint64_t rdx;   // offset 16
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;   // offset 104 ← first pushed, highest address
};


struct __vmm_context_t;  // forward declaration

// -----------------------------------------------------------------------
// Per-processor virtual CPU state.
// One of these is allocated for each logical processor.
// -----------------------------------------------------------------------
struct __vcpu_t
{
    __vmcb_t*   vmcb;               // virtual address of this vCPU's VMCB
    uint64_t    vmcb_physical;

    void*       host_save_area;     // virtual address of 4 KB host save area
    uint64_t    host_save_area_pa;

    uint32_t    processor_index;    // index of the logical processor this vCPU runs on
    void*       vmm_stack;
    void*       vmm_stack_alloc;
    __ia32_efer_t shadow_efer;
    __vmm_context_t* vmm_context;
};

// -----------------------------------------------------------------------
// Per-system VMM context.
// Shared across all vCPUs; accessible from the VMM stack during #VMEXIT.
// -----------------------------------------------------------------------
struct __vmm_context_t
{
    __vcpu_t** vcpu_table;
    uint32_t    processor_count;

    void* msr_bitmap;
    uint64_t    msr_bitmap_pa;

    // Global identity NPT (see SVM/Npt.h for __npt_global_t). Allocated once
    // at SVM::Initialize; all vCPUs share it. Pointer type is void* so this
    // header doesn't need to include Npt.h.
    void*    npt_global;
};


// -----------------------------------------------------------------------
// VMM stack layout.
//
// Each vCPU gets a VMM_STACK_SIZE-byte contiguous region.  The vcpu pointer
// is stored at the LOWEST address of that region so the exit handler can
// find it by aligning RSP down to VMM_STACK_SIZE:
//
// The actual stack grows downward from (base + VMM_STACK_SIZE - 8).
// -----------------------------------------------------------------------
struct __vmm_stack_t
{
    __vcpu_t* vcpu;
};
