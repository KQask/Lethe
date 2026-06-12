#pragma once

// -----------------------------------------------------------------------
// AMD SVM MSR addresses and bitfield unions
// Reference: AMD64 APM Vol.2, Appendix B
// -----------------------------------------------------------------------

// MSR addresses
#define MSR_EFER            0xC0000080  // Extended Feature Enable Register
#define MSR_STAR            0xC0000081  // SYSCALL segment selectors
#define MSR_LSTAR           0xC0000082  // SYSCALL target RIP (64-bit mode)
#define MSR_CSTAR           0xC0000083  // SYSCALL target RIP (compat mode)
#define MSR_SFMASK          0xC0000084  // SYSCALL RFLAGS mask
#define MSR_FS_BASE         0xC0000100
#define MSR_GS_BASE         0xC0000101
#define MSR_SHADOW_GS_BASE  0xC0000102  // KernelGsBase (swapped with GS_BASE by SWAPGS)

#define MSR_SYSENTER_CS     0x174
#define MSR_SYSENTER_ESP    0x175
#define MSR_SYSENTER_EIP    0x176
#define MSR_PAT             0x277

#define MSR_VM_CR           0xC0010114  // SVM control: check SVMDIS before enabling SVM
#define MSR_VM_HSAVE_PA     0xC0010117  // write physical address of host save area here

// Bit helpers
#define EFER_SVME           (1ULL << 12) // set in EFER to enable SVM on this core
#define VM_CR_SVMDIS        (1ULL <<  4) // SVM disabled in firmware if this bit is set
#define VM_CR_LOCK          (1ULL <<  3) // if set alongside SVMDIS, cannot be cleared

union __ia32_efer_t
{
    uint64_t raw;
    struct
    {
        uint64_t sce   : 1;
        uint64_t _r0   : 7;  // bits 1-7 reserved
        uint64_t lme   : 1;
        uint64_t _r1   : 1;  // bit  9 reserved
        uint64_t lma   : 1;
        uint64_t nxe   : 1;
        uint64_t svme  : 1;
        uint64_t lmsle : 1;
        uint64_t ffxsr : 1;
        uint64_t tce   : 1;
        uint64_t _r2   : 48; // bits 16-63 reserved
    } bits;
};
