#pragma once

// -----------------------------------------------------------------------
// RFLAGS bitfield union
// Reference: AMD64 APM Vol.1 §3.8  /  Intel SDM Vol.1 §3.4.3
//
// -----------------------------------------------------------------------
union __rflags_t
{
    uint64_t raw;
    struct
    {
        uint64_t cf   : 1;
        uint64_t _r0  : 1;
        uint64_t pf   : 1;
        uint64_t _r1  : 1;
        uint64_t af   : 1;
        uint64_t _r2  : 1;
        uint64_t zf   : 1;
        uint64_t sf   : 1;
        uint64_t tf   : 1;
        uint64_t if_  : 1;
        uint64_t df   : 1;
        uint64_t of   : 1;
        uint64_t iopl : 2;
        uint64_t nt   : 1;
        uint64_t _r3  : 1;
        uint64_t rf   : 1;
        uint64_t vm   : 1;
        uint64_t ac   : 1;
        uint64_t vif  : 1;
        uint64_t vip  : 1;
        uint64_t id   : 1;
        uint64_t _r4  : 42; // bits 22-63 reserved
    } bits;
};
