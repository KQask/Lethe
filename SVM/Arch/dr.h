#pragma once

// -----------------------------------------------------------------------
// Debug register bitfield unions
// AMD64 behaviour is identical to Intel for DR6/DR7.
// Reference: AMD64 APM Vol.2 §13.1
//
// -----------------------------------------------------------------------

union __dr6_t
{
    uint64_t raw;
    struct
    {
        uint64_t b0   : 1;
        uint64_t b1   : 1;
        uint64_t b2   : 1;
        uint64_t b3   : 1;
        uint64_t _r0  : 9;  // bits 4-12 reserved (always 1 on reads)
        uint64_t bd   : 1;
        uint64_t bs   : 1;
        uint64_t bt   : 1;
        uint64_t rtm  : 1;
        uint64_t _r1  : 47; // bits 17-63 reserved
    } bits;
};

union __dr7_t
{
    uint64_t raw;
    struct
    {
        uint64_t l0   : 1;
        uint64_t g0   : 1;
        uint64_t l1   : 1;
        uint64_t g1   : 1;
        uint64_t l2   : 1;
        uint64_t g2   : 1;
        uint64_t l3   : 1;
        uint64_t g3   : 1;
        uint64_t le   : 1;
        uint64_t ge   : 1;
        uint64_t _r0  : 1;  // bit 10 reserved (always 1)
        uint64_t rtm  : 1;
        uint64_t _r1  : 1;  // bit 12 reserved
        uint64_t gd   : 1;
        uint64_t _r2  : 2;  // bits 14-15 reserved
        uint64_t cond0: 2;
        uint64_t len0 : 2;
        uint64_t cond1: 2;  // bits 20-21
        uint64_t len1 : 2;  // bits 22-23
        uint64_t cond2: 2;  // bits 24-25
        uint64_t len2 : 2;  // bits 26-27
        uint64_t cond3: 2;  // bits 28-29
        uint64_t len3 : 2;  // bits 30-31
        uint64_t _r3  : 32; // bits 32-63 reserved
    } bits;
};
