#pragma once

// VMCB Save State Area (bytes 0x400-0xFFF). AMD64 APM Vol.2, App. B, Table B-2.
#pragma pack(push, 1)
struct __svm_segment_t
{
    uint16_t selector;  // segment selector (e.g. value of CS register)
    uint16_t attrib;    // packed 12-bit attributes: low byte = type/S/DPL/P, high nibble = AVL/L/DB/G
    uint32_t limit;     // segment limit (expanded from 20-bit raw)
    uint64_t base;      // segment base address
};
static_assert(sizeof(__svm_segment_t) == 0x10, "__svm_segment_t must be 16 bytes");


struct __vmcb_save_state_t
{
    // ---- Segment descriptors (10 × 16 bytes = 0xA0) ----
    __svm_segment_t  es;            // 0x000
    __svm_segment_t  cs;            // 0x010
    __svm_segment_t  ss;            // 0x020
    __svm_segment_t  ds;            // 0x030
    __svm_segment_t  fs;            // 0x040
    __svm_segment_t  gs;            // 0x050
    __svm_segment_t  gdtr;          // 0x060  (only base + limit used by CPU)
    __svm_segment_t  ldtr;          // 0x070
    __svm_segment_t  idtr;          // 0x080  (only base + limit used by CPU)
    __svm_segment_t  tr;            // 0x090

    uint8_t          reserved_0[0x30]; // 0x0A0

    // ---- MSRs ----
    uint64_t         efer;          // 0x0D0  must have EFER_SVME set in guest

    uint8_t          reserved_1[0x70]; // 0x0D8

    // ---- Control registers ----
    uint64_t         cr4;           // 0x148
    uint64_t         cr3;           // 0x150
    uint64_t         cr0;           // 0x158

    // ---- Debug registers ----
    uint64_t         dr7;           // 0x160
    uint64_t         dr6;           // 0x168

    // ---- General execution state ----
    uint64_t         rflags;        // 0x170
    uint64_t         rip;           // 0x178  guest instruction pointer on entry / at #VMEXIT

    uint8_t          reserved_2[0x58]; // 0x180

    uint64_t         rsp;           // 0x1D8  guest stack pointer

    uint8_t          reserved_3[0x18]; // 0x1E0

    uint64_t         rax;           // 0x1F8  only GPR automatically saved/restored by hardware

    // ---- SYSCALL / SYSENTER MSRs ----
    uint64_t         star;          // 0x200
    uint64_t         lstar;         // 0x208
    uint64_t         cstar;         // 0x210
    uint64_t         sfmask;        // 0x218
    uint64_t         kernel_gs_base;// 0x220
    uint64_t         sysenter_cs;   // 0x228
    uint64_t         sysenter_esp;  // 0x230
    uint64_t         sysenter_eip;  // 0x238

    // ---- Misc ----
    uint64_t         cr2;           // 0x240  page-fault linear address

    uint8_t          reserved_4[0x20]; // 0x248

    uint64_t         pat;           // 0x268  page attribute table MSR
    uint64_t         dbgctl;        // 0x270  IA32_DEBUGCTL
    uint64_t         br_from;       // 0x278
    uint64_t         br_to;         // 0x280
    uint64_t         last_excp_from;// 0x288
    uint64_t         last_excp_to;  // 0x290

    uint8_t          reserved_5[0x968];
};
static_assert(sizeof(__vmcb_save_state_t) == 0xC00, "__vmcb_save_state_t must be 0xC00 bytes");
#pragma pack(pop)
