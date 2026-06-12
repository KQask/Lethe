// hypercall.exe minimal Hv2 liveness check.
//
// Pings the hypervisor via VMMCALL. If Hv2 is loaded, the magic key gets
// echoed back; otherwise the call traps as a normal #UD on bare metal.

#include <Windows.h>
#include <cstdio>
#include "../SVM/Arch/Hypercalls.h"

int main()
{
    uint64_t r = hv2_vmmcall(HV2_SECRET_KEY, hv2_ping, nullptr);

    if (r != 0xDEAD4876) {
        printf("[-] hv2_ping returned 0x%llX (expected 0xDEAD4876)\n", r);
        printf("    Hv2 is not loaded. Start it with `sc start lethe`.\n");
        return 1;
    }
    printf("[+] hv2_ping  -> 0x%llX  (hypervisor active)\n", r);
    return 0;
}
