#include <ntifs.h>
#include "../Globals.h"

static __vmm_context_t* g_vmm_context = nullptr;
static KDPC* g_launch_dpcs = nullptr;  // one per processor, for launch DPCs


BOOLEAN SVM::CheckSupport()
{
    int registers[4] = { 0 };
    __cpuid(registers, CPUID_MAX_STANDARD_FN_NUMBER_AND_VENDOR_STRING);
    if ((registers[1] != 'htuA') || (registers[3] != 'itne') || (registers[2] != 'DMAc')) {
        return FALSE;
    }
    __cpuid(registers, CPUID_PROCESSOR_AND_PROCESSOR_FEATURE_IDENTIFIERS_EX);
    if (!(registers[2] & CPUID_FN8000_0001_ECX_SVM)) {
        return FALSE;
    }
    __cpuid(registers, CPUID_SVM_FEATURES);
    if (!(registers[3] & CPUID_FN8000_000A_EDX_NP)) {
        return FALSE;
    }

    const ULONG64 vmcr = __readmsr(SVM_MSR_VM_CR);
    if (vmcr & SVM_VM_CR_SVMDIS) {
        return FALSE;
    }

    return TRUE;
}


// memhv-style: pin thread to each processor in turn, run callback, revert.
// See https://github.com/SamuelTulach/memhv (Utils.cpp).
static NTSTATUS ExecuteOnEachProcessor(NTSTATUS(*callback)(PVOID), PVOID context) {
    ULONG const numProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    for (ULONG i = 0; i < numProcessors; i++) {
        PROCESSOR_NUMBER processorNumber;
        NTSTATUS status = KeGetProcessorNumberFromIndex(i, &processorNumber);
        if (!NT_SUCCESS(status))
            return status;

        GROUP_AFFINITY affinity = {};
        affinity.Group = processorNumber.Group;
        affinity.Mask = 1ULL << processorNumber.Number;
        affinity.Reserved[0] = affinity.Reserved[1] = affinity.Reserved[2] = 0;

        GROUP_AFFINITY oldAffinity;
        KeSetSystemGroupAffinityThread(&affinity, &oldAffinity);

        status = callback(context);

        KeRevertToUserGroupAffinityThread(&oldAffinity);

        if (!NT_SUCCESS(status))
            return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS PerCoreSetupCallback(PVOID context) {
    __vmm_context_t* ctx = (__vmm_context_t*)context;
    ULONG idx = KeGetCurrentProcessorNumberEx(NULL);
    __vcpu_t* vCpu = ctx->vcpu_table[idx];

    // Initialize shadow_efer from THIS processor's EFER (before enabling SVM).
    __ia32_efer_t host_efer = { __readmsr(MSR_EFER) };
    host_efer.bits.svme = 0;  // guest should not see SVME
    vCpu->shadow_efer.raw = host_efer.raw;

    __ia32_efer_t efer = { __readmsr(MSR_EFER) };
    efer.bits.svme = 1;
    __writemsr(MSR_EFER, efer.raw);
    __writemsr(MSR_VM_HSAVE_PA, vCpu->host_save_area_pa);
    init_vmcb(vCpu, ctx);

    log_debug("[Core %u] SVM setup done (EFER, VM_HSAVE_PA, init_vmcb)", idx);
    return STATUS_SUCCESS;
}

static void LaunchDpcRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID Arg1, PVOID Arg2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    __vmm_context_t* context = (__vmm_context_t*)DeferredContext;
    ULONG idx = KeGetCurrentProcessorNumberEx(NULL);
    __vcpu_t* vCpu = context->vcpu_table[idx];
    svm_launch(vCpu->vmcb_physical, &vCpu->vmcb->save.rsp,
               (ULONG64)vCpu->vmm_stack + VMM_STACK_SIZE - 8,
               vCpu->host_save_area_pa);
    // never returns
}


void SVM::Initialize(PVOID context)
{
    UNREFERENCED_PARAMETER(context);

    __vmm_context_t* vmm_context = (__vmm_context_t*)ExAllocatePoolWithTag(NonPagedPool, sizeof(__vmm_context_t), 'FMem');
    RtlZeroMemory(vmm_context, sizeof(__vmm_context_t));

    ULONG count = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    vmm_context->processor_count = count;

    vmm_context->vcpu_table = (__vcpu_t**)ExAllocatePoolWithTag(NonPagedPool, sizeof(__vcpu_t*) * count, 'MmSt');

    PHYSICAL_ADDRESS max;
    max.QuadPart = MAXULONG64;

    vmm_context->msr_bitmap = MmAllocateContiguousMemory(0x2000, max);
    RtlZeroMemory(vmm_context->msr_bitmap, 0x2000);

    ((uint8_t*)vmm_context->msr_bitmap)[0x0820] |= 3;    // EFER (0xC0000080): bits 0-1 = read+write
    ((uint8_t*)vmm_context->msr_bitmap)[0x0840] |= 0xF;  // FS_BASE (bits 0-1) + GS_BASE (bits 2-3)

    NTSTATUS npg = npt_global_init(vmm_context);
    if (!NT_SUCCESS(npg)) {
        log_error("[Hv2] npt_global_init failed: 0x%X", npg);
        return;
    }

    // If CR4.FSGSBASE is set, FS/GS_BASE reads/writes execute without #GP at
    // CPL=3 (no syscall), so intercepting them just produces noise.
    if (__readcr4() & (1ULL << 16)) {
        ((uint8_t*)vmm_context->msr_bitmap)[0x0840] &= ~0xF;
        log_debug("[Hv2] FSGSBase active - FS/GS_BASE intercepts removed");
    }

    vmm_context->msr_bitmap_pa = MmGetPhysicalAddress(vmm_context->msr_bitmap).QuadPart;

    g_vmm_context = vmm_context;
    g_launch_dpcs = (KDPC*)ExAllocatePoolWithTag(NonPagedPool, count * sizeof(KDPC), 'KeDp');
    RtlZeroMemory(g_launch_dpcs, count * sizeof(KDPC));
    for (ULONG i = 0; i < count; i++) {
        KeInitializeDpc(&g_launch_dpcs[i], LaunchDpcRoutine, vmm_context);
    }

    for (ULONG i = 0; i < count; i++) {

        __vcpu_t* vCpu = (__vcpu_t*)ExAllocatePoolWithTag(NonPagedPool, sizeof(__vcpu_t), 'Thrd');
        RtlZeroMemory(vCpu, sizeof(__vcpu_t));
        vmm_context->vcpu_table[i] = vCpu;
        vCpu->vmm_context = vmm_context;


        __vmcb_t* vmcb = (__vmcb_t*)MmAllocateContiguousMemory(0x1000, max);
        RtlZeroMemory(vmcb, sizeof(__vmcb_t));
        vCpu->vmcb = vmcb;
        vCpu->vmcb_physical = MmGetPhysicalAddress(vmcb).QuadPart;

        PVOID hostVmcb = MmAllocateContiguousMemory(0x1000, max);
        RtlZeroMemory(hostVmcb, sizeof(__vmcb_t));
        vCpu->host_save_area = hostVmcb;
        vCpu->host_save_area_pa = MmGetPhysicalAddress(hostVmcb).QuadPart;

        PVOID rawStack = ExAllocatePoolWithTag(NonPagedPool, VMM_STACK_SIZE * 2, 'KStk');
        RtlZeroMemory(rawStack, VMM_STACK_SIZE * 2);
        vCpu->processor_index = i;

        __vmm_stack_t* vmmStack = (__vmm_stack_t*)(((ULONG64)rawStack + VMM_STACK_SIZE - 1) & ~(ULONG64)(VMM_STACK_SIZE - 1));
        vCpu->vmm_stack = vmmStack;

        vCpu->vmm_stack_alloc = rawStack;

        vmmStack->vcpu = vCpu;

        log_debug("[Core %u] vCPU=0x%p  VMCB VA=0x%p PA=0x%llX",
                  i, vCpu, vmcb, vCpu->vmcb_physical);
        log_debug("[Core %u] VMM stack base=0x%p  host RSP=0x%llX",
                  i, vmmStack, (ULONG64)vmmStack + VMM_STACK_SIZE - 8);
    }

    // Phase 1: Setup on each core (pin thread to each processor in turn).
    NTSTATUS setupStatus = ExecuteOnEachProcessor(PerCoreSetupCallback, vmm_context);
    if (!NT_SUCCESS(setupStatus)) {
        log_error("[Hv2] ExecuteOnEachProcessor (setup) failed: 0x%X", setupStatus);
        return;
    }

    // Phase 2: DPC each other core into the guest; this core takes off here
    // and doesn't return.
    ULONG current = KeGetCurrentProcessorNumberEx(NULL);
    for (ULONG i = 0; i < count; i++) {
        if (i == current)
            continue;
        KeSetTargetProcessorDpc(&g_launch_dpcs[i], (CCHAR)i);
        KeInsertQueueDpc(&g_launch_dpcs[i], NULL, NULL);
    }

    __vcpu_t* vCpu = vmm_context->vcpu_table[current];
    svm_launch(vCpu->vmcb_physical, &vCpu->vmcb->save.rsp,
               (ULONG64)vCpu->vmm_stack + VMM_STACK_SIZE - 8,
               vCpu->host_save_area_pa);
}

void SVM::Disable()
{
    return;
}

void SVM::Teardown()
{
    if (!g_vmm_context)
        return;

    for (ULONG i = 0; i < g_vmm_context->processor_count; i++)
    {
        __vcpu_t* vcpu = g_vmm_context->vcpu_table[i];
        if (!vcpu) continue;

        if (vcpu->vmcb)            MmFreeContiguousMemory(vcpu->vmcb);
        if (vcpu->host_save_area)  MmFreeContiguousMemory(vcpu->host_save_area);
        if (vcpu->vmm_stack_alloc) ExFreePoolWithTag(vcpu->vmm_stack_alloc, 'KStk');

        ExFreePoolWithTag(vcpu, 'Thrd');
    }

    MmFreeContiguousMemory(g_vmm_context->msr_bitmap);
    ExFreePoolWithTag(g_vmm_context->vcpu_table, 'MmSt');
    ExFreePoolWithTag(g_vmm_context, 'FMem');
    if (g_launch_dpcs) {
        ExFreePoolWithTag(g_launch_dpcs, 'KeDp');
        g_launch_dpcs = nullptr;
    }

    g_vmm_context = nullptr;
}
