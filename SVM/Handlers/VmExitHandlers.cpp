#include "../../Globals.h"
#include "../Npt.h"

static void inject_ud(__vcpu_t* vcpu)
{
    __svm_event_inject_t ev = {};
    ev.bits.vector = EXVEC_UD;
    ev.bits.type   = 3;
    ev.bits.valid  = 1;
    vcpu->vmcb->control.event_inject = ev.flags;
}

static void inject_gp(__vcpu_t* vcpu)
{
    __svm_event_inject_t ev = {};
    ev.bits.vector           = EXVEC_GP;
    ev.bits.type             = 3;
    ev.bits.error_code_valid = 1;
    ev.bits.valid            = 1;
    ev.bits.error_code = 0;
    vcpu->vmcb->control.event_inject = ev.flags;
}

static void handleSvmInstruction(__vcpu_t* vcpu);
static void handle_rdmsr  (__vcpu_t* vcpu, __guest_registers_t* regs);
static void handle_wrmsr  (__vcpu_t* vcpu, __guest_registers_t* regs);
static void handle_vmmcall(__vcpu_t* vcpu, __guest_registers_t* regs);
static void handle_shutdown(__vcpu_t* vcpu);
static void handle_invd   (__vcpu_t* vcpu);
static void handle_wbinvd (__vcpu_t* vcpu);

// true = success (advance RIP); false = exception injected (do not advance).
static bool hv2_handle_ping(__vcpu_t* vcpu, __guest_registers_t* regs);


void vmexit_handler(__vmm_stack_t* stack, __guest_registers_t* regs)
{
    __vcpu_t*             vcpu = stack->vcpu;
    __vmcb_t*             vmcb = vcpu->vmcb;

    const auto exit_code = static_cast<__svm_exit_code_e>(vmcb->control.exit_code);

    switch (exit_code)
    {
    case svm_exit_exception_db: handle_db(vcpu); break;
    case svm_exit_rdmsr:
        // exit_info_1 bit 0: 0 = RDMSR, 1 = WRMSR  (AMD APM Vol.2 15.11)
        if (vmcb->control.exit_info_1 & 1)
            handle_wrmsr(vcpu, regs);
        else
            handle_rdmsr(vcpu, regs);
        break;
    case svm_exit_vmmcall:  handle_vmmcall(vcpu, regs); break;
    case svm_exit_shutdown: handle_shutdown(vcpu);       break;

    // INVD without writeback can corrupt host memory; promote to WBINVD.
    case svm_exit_invd:     handle_invd(vcpu);           break;
    case svm_exit_wbinvd:   handle_wbinvd(vcpu);         break;

    // SKINIT could establish a new root of trust and escape us.
    case svm_exit_skinit:   inject_gp(vcpu);              break;

    case svm_exit_npf:      handle_npf(vcpu);             break;

    case svm_exit_vmrun: handleSvmInstruction(vcpu); break;
    case svm_exit_vmload: handleSvmInstruction(vcpu); break;
    case svm_exit_vmsave:
    case svm_exit_stgi:
    case svm_exit_clgi:

        if (vcpu->shadow_efer.bits.svme == 0) {
            inject_ud(vcpu);
        }
        else {
            inject_gp(vcpu);
        }
        break;

    case svm_exit_invalid:
        KeBugCheckEx(
            0xDEADDEAD,
            (ULONG_PTR)vcpu->processor_index,
            (ULONG_PTR)vmcb->control.exit_info_1,
            (ULONG_PTR)vmcb->control.exit_info_2,
            (ULONG_PTR)vmcb
        );
        break;

    default:
        log_error("[Hv2] Unhandled #VMEXIT 0x%llX on core %u  info1=0x%llX  info2=0x%llX",
                  vmcb->control.exit_code,
                  vcpu->processor_index,
                  vmcb->control.exit_info_1,
                  vmcb->control.exit_info_2);
        vmcb->save.rip = vmcb->control.next_rip;
        break;
    }
}


static void handleSvmInstruction(__vcpu_t* vcpu) {
    if (vcpu->shadow_efer.bits.svme == 0) {
        inject_ud(vcpu);
    }
    else {
        inject_gp(vcpu);
    }
}


static void handle_rdmsr(__vcpu_t* vcpu, __guest_registers_t* regs)
{
    uint32_t msr = regs->rcx & 0xFFFFFFFF;
    uint64_t val = 0;

    __try {
        if (msr == MSR_EFER) {
            val = vcpu->shadow_efer.raw;
        }
        else if (msr == MSR_STAR) {
            val = vcpu->vmcb->save.star;
        }
        else if (msr == MSR_LSTAR) {
            val = vcpu->vmcb->save.lstar;
        }
        else if (msr == MSR_CSTAR) {
            val = vcpu->vmcb->save.cstar;
        }
        else if (msr == MSR_SFMASK) {
            val = vcpu->vmcb->save.sfmask;
        }
        else if (msr == MSR_SHADOW_GS_BASE) {
            val = vcpu->vmcb->save.kernel_gs_base;
        }
        else if (msr == MSR_SYSENTER_CS) {
            val = vcpu->vmcb->save.sysenter_cs;
        }
        else if (msr == MSR_SYSENTER_ESP) {
            val = vcpu->vmcb->save.sysenter_esp;
        }
        else if (msr == MSR_SYSENTER_EIP) {
            val = vcpu->vmcb->save.sysenter_eip;
        }
        else if (msr == MSR_PAT) {
            val = vcpu->vmcb->save.pat;
        }
        else if (msr == MSR_FS_BASE) {
            val = vcpu->vmcb->save.fs.base;
        }
        else if (msr == MSR_GS_BASE) {
            val = vcpu->vmcb->save.gs.base;
        }
        else {
            // Anything not on the MSRPM allowlist (EFER/FS_BASE/GS_BASE) lands
            // here because we forced an intercept. We can't just __readmsr it:
            // a non-existent MSR #GPs, and a manually-mapped image has no
            // .pdata for SEH to unwind, so the #GP bugchecks 0x1AA. Easiest
            // to fake a zero read and move on.
            val = 0;
        }

        vcpu->vmcb->save.rax = val & 0xFFFFFFFF;
        regs->rdx = val >> 32;

        vcpu->vmcb->save.rip = vcpu->vmcb->control.next_rip;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        inject_gp(vcpu);
    }
}

// Don't __writemsr from the host. PatchGuard watches the critical MSRs
// and will bugcheck 0x109 (Arg4=7) on modification. Update VMCB save
// state instead and the value applies on next VMRUN.
static void handle_wrmsr(__vcpu_t* vcpu, __guest_registers_t* regs)
{
    uint32_t msr = regs->rcx & 0xFFFFFFFF;
    uint64_t val = ((uint64_t)regs->rdx << 32 | (vcpu->vmcb->save.rax & 0xFFFFFFFF));
    __try {
        if (msr == MSR_EFER) {
            vcpu->shadow_efer.raw = val;
            vcpu->vmcb->save.efer = val;
        }
        else if (msr == MSR_STAR) {
            vcpu->vmcb->save.star = val;
        }
        else if (msr == MSR_LSTAR) {
            vcpu->vmcb->save.lstar = val;
        }
        else if (msr == MSR_CSTAR) {
            vcpu->vmcb->save.cstar = val;
        }
        else if (msr == MSR_SFMASK) {
            vcpu->vmcb->save.sfmask = val;
        }
        else if (msr == MSR_SHADOW_GS_BASE) {
            vcpu->vmcb->save.kernel_gs_base = val;
        }
        else if (msr == MSR_SYSENTER_CS) {
            vcpu->vmcb->save.sysenter_cs = val;
        }
        else if (msr == MSR_SYSENTER_ESP) {
            vcpu->vmcb->save.sysenter_esp = val;
        }
        else if (msr == MSR_SYSENTER_EIP) {
            vcpu->vmcb->save.sysenter_eip = val;
        }
        else if (msr == MSR_PAT) {
            vcpu->vmcb->save.pat = val;
        } else if (msr == MSR_FS_BASE) {
            vcpu->vmcb->save.fs.base = val;
        } else if (msr == MSR_GS_BASE) {
            vcpu->vmcb->save.gs.base = val;
        }
        else {
            // Discard. __writemsr would #GP and bugcheck (see handle_rdmsr).
        }
        vcpu->vmcb->save.rip = vcpu->vmcb->control.next_rip;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        inject_gp(vcpu);
    }
}

static bool hv2_handle_ping(__vcpu_t* vcpu, __guest_registers_t* regs)
{
    UNREFERENCED_PARAMETER(regs);
    vcpu->vmcb->save.rax = 0xDEAD4876;
    return true;
}


static void handle_vmmcall(__vcpu_t* vcpu, __guest_registers_t* regs)
{
    uint64_t next_rip = vcpu->vmcb->control.next_rip;
    if (next_rip == 0)
        next_rip = vcpu->vmcb->save.rip + 3;

    if (regs->rcx != HV2_SECRET_KEY) {
        inject_gp(vcpu);
        return;
    }

    bool ok = false;
    switch (static_cast<HYPERCALL_ID>(regs->rdx))
    {
    case hv2_ping: ok = hv2_handle_ping(vcpu, regs); break;
    default:
        inject_gp(vcpu);
        return;
    }

    if (ok)
        vcpu->vmcb->save.rip = next_rip;
}

static void handle_invd(__vcpu_t* vcpu)
{
    __wbinvd();
    vcpu->vmcb->save.rip = vcpu->vmcb->control.next_rip;
}

static void handle_wbinvd(__vcpu_t* vcpu)
{
    __wbinvd();
    vcpu->vmcb->save.rip = vcpu->vmcb->control.next_rip;
}

// Guest triple-faulted. Nothing we can do; park the core.
static void handle_shutdown(__vcpu_t* vcpu)
{
    log_error("[Hv2] Guest triple-fault on core %u", vcpu->processor_index);
    for (;;) { __halt(); }
}
