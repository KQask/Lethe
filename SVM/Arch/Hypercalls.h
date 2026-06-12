#pragma once
#include <cstdint>
#pragma pack(push, 1)
// -----------------------------------------------------------------------
// Hv2 Custom Hypercall IDs
// Passed by the guest to the hypervisor via the VMMCALL instruction.
// -----------------------------------------------------------------------
#define HV2_SECRET_KEY    0x48765F534543ULL   // "Hv_SEC" shared compile-time secret

#ifndef _NTDDK_
typedef long NTSTATUS;
#endif

enum HYPERCALL_ID : uint64_t {
    hv2_ping = 0x48763201,
};

extern "C" uint64_t __stdcall hv2_vmmcall(uint64_t key, HYPERCALL_ID id, void* buffer);

#pragma pack(pop)
