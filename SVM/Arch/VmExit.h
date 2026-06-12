#pragma once

// -----------------------------------------------------------------------
// AMD SVM #VMEXIT exit codes
// Written by the CPU into vmcb.control.exit_code on every #VMEXIT.
// Reference: AMD64 APM Vol.2, Appendix C
// -----------------------------------------------------------------------
enum __svm_exit_code_e : uint64_t
{
    // CR read/write intercepts (bit N in intercept_cr_read/write)
    svm_exit_cr0_read       = 0x000,
    svm_exit_cr0_write      = 0x010,
    // CR1-CR15 follow the same pattern (+N for CRN)

    // Exception intercepts (bit N in intercept_exceptions)
    svm_exit_excp_base      = 0x040,    // add vector number (0-31)
    svm_exit_exception_db   = 0x041,


    // Instruction intercepts (intercept_misc_1)
    svm_exit_cpuid          = 0x072,
    svm_exit_invd           = 0x076,
    svm_exit_hlt            = 0x078,
    svm_exit_invlpg         = 0x079,    // NOTE: was wrongly 0x076 (that is INVD, not INVLPG)
    svm_exit_rdmsr          = 0x07C,
    svm_exit_wrmsr          = 0x07C,    // same code as rdmsr, distinguished by exit_info_1 bit 0
    svm_exit_shutdown       = 0x07F,    // triple fault

    // SVM instruction intercepts (intercept_misc_2)
    svm_exit_vmrun          = 0x080,
    svm_exit_vmmcall        = 0x081,    // guest executed VMMCALL
    svm_exit_vmload         = 0x082,
    svm_exit_vmsave         = 0x083,
    svm_exit_stgi           = 0x084,
    svm_exit_clgi           = 0x085,
    svm_exit_skinit         = 0x086,
    svm_exit_wbinvd         = 0x089,    // writeback + invalidate cache

    // Nested paging fault (requires NPT enabled)
    svm_exit_npf            = 0x400,

    // Sentinel: invalid guest state (check VMCB for details)
    svm_exit_invalid        = (uint64_t)-1,
};

// -----------------------------------------------------------------------
// exception or interrupt into the guest on the next VMRUN.
// Reference: AMD64 APM Vol.2 §15.20
//
// Usage example (inject #UD = undefined opcode, vector 6):
//
//   __svm_event_inject_t ev = {};
//   ev.bits.vector           = EXVEC_UD;
//   ev.bits.type             = 3;   // 3 = hardware exception
//   ev.bits.error_code_valid = 0;   // #UD has no error code
//   ev.bits.valid            = 1;
//   vcpu->vmcb->control.event_inject = ev.flags;
// -----------------------------------------------------------------------
union __svm_event_inject_t
{
    struct
    {
        uint64_t vector           :  8;  // exception/interrupt vector (0-255)
        uint64_t type             :  3;  // 2=NMI, 3=hardware exception, 4=software int
        uint64_t error_code_valid :  1;  // 1 if the exception pushes an error code
        uint64_t reserved         : 19;
        uint64_t valid            :  1;  // must be 1 for the CPU to inject on next VMRUN
        uint64_t error_code       : 32;  // error code (only used when error_code_valid=1)
    } bits;
    uint64_t flags;
};

// -----------------------------------------------------------------------
// Exception vectors (x86-64, same on AMD and Intel)
// -----------------------------------------------------------------------
enum x86_exception_vector_e : uint64_t
{
    EXVEC_DE  = 0,
    EXVEC_DB  = 1,
    EXVEC_NMI = 2,
    EXVEC_BP  = 3,
    EXVEC_OF  = 4,
    EXVEC_BR  = 5,
    EXVEC_UD  = 6,
    EXVEC_NM  = 7,
    EXVEC_DF  = 8,
    EXVEC_TS  = 10,
    EXVEC_NP  = 11,
    EXVEC_SS  = 12,
    EXVEC_GP  = 13,
    EXVEC_PF  = 14,
    EXVEC_MF  = 16,
    EXVEC_AC  = 17,
    EXVEC_MC  = 18,
    EXVEC_XM  = 19,
};

