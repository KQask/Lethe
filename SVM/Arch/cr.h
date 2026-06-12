#pragma once

// -----------------------------------------------------------------------
// Control register bitfield unions
// AMD64 layout is identical to Intel x86-64 for CR0/CR3/CR4.
// Reference: AMD64 APM Vol.2 §3.1
//
// -----------------------------------------------------------------------

union __cr0_t
{
    uint64_t raw;
    struct
    {
        uint64_t pe   : 1;
        uint64_t mp   : 1;
        uint64_t em   : 1;
        uint64_t ts   : 1;
        uint64_t et   : 1;
        uint64_t ne   : 1;
        uint64_t _r0  : 10; // bits 6-15 reserved
        uint64_t wp   : 1;
        uint64_t _r1  : 1;  // bit 17 reserved
        uint64_t am   : 1;
        uint64_t _r2  : 10; // bits 19-28 reserved
        uint64_t nw   : 1;
        uint64_t cd   : 1;
        uint64_t pg   : 1;
        uint64_t _r3  : 32; // bits 32-63 reserved (must be 0)
    } bits;
};

union __cr4_t
{
    uint64_t raw;
    struct
    {
        uint64_t vme        : 1;
        uint64_t pvi        : 1;
        uint64_t tsd        : 1;
        uint64_t de         : 1;
        uint64_t pse        : 1;
        uint64_t pae        : 1;
        uint64_t mce        : 1;
        uint64_t pge        : 1;
        uint64_t pce        : 1;
        uint64_t osfxsr     : 1;
        uint64_t osxmmexcpt : 1;
        uint64_t umip       : 1;
        uint64_t _r0        : 1;  // bit 12 reserved
        uint64_t vmxe       : 1;
        uint64_t smxe       : 1;
        uint64_t _r1        : 1;  // bit 15 reserved
        uint64_t fsgsbase   : 1;
        uint64_t pcide      : 1;
        uint64_t osxsave    : 1;
        uint64_t _r2        : 1;  // bit 19 reserved
        uint64_t smep       : 1;
        uint64_t smap       : 1;
        uint64_t _r3        : 42; // bits 22-63 reserved
    } bits;
};

// CR3 for standard 4-level paging (IA-32e / long mode)
union __cr3_t
{
    uint64_t raw;
    struct
    {
        uint64_t _r0  : 3;  // bits 0-2 ignored
        uint64_t pwt  : 1;
        uint64_t pcd  : 1;
        uint64_t _r1  : 7;  // bits 5-11 ignored
        uint64_t pfn  : 40;
        uint64_t _r2  : 12; // bits 52-63 reserved
    } bits;
};

