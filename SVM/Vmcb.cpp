#include "../Globals.h"

static void fill_segment(__svm_segment_t& out,
                         uint16_t         selector,
                         const __pseudo_descriptor_64_t& gdtr)
{
    out.selector = selector;
    uint64_t offset = selector & SELECTOR_RPL_MASK;
    if ((offset) == 0)
    {
        __segment_access_rights_t ar = {};
        ar.bits.unusable = 1;
        out.attrib = static_cast<uint16_t>(ar.flags);
        out.limit  = 0;
        out.base   = 0;
        return;
    }
    
    auto* desc = reinterpret_cast<__segment_descriptor_64_t*>(gdtr.base + offset);

    if (desc->descriptor_type == 0)   // system segment (TSS/LDT): 16-byte descriptor
    {
        uint64_t lower = desc->base_low | (desc->base_middle << 16) | (desc->base_high << 24);
        uint64_t upper = (uint64_t)desc->base_upper << 32;
        out.base = lower | upper;
    }
    else
    {
        uint32_t base = desc->base_low | (desc->base_middle << 16) | (desc->base_high << 24);
        out.base = base;
    }

    uint64_t lowerLimit = desc->limit_low;
    uint64_t lowerUpper = desc->limit_high << 16;
    uint32_t limit = lowerLimit | lowerUpper;

    if (desc->granularity == 1) {
        limit = (limit << 12) | 0xFFF;
    }

    out.limit = limit;

    uint32_t attrib = __load_ar(selector);
    attrib = ((attrib >> 8) & 0xFF) | ((attrib >> 12) & 0xF00);

    out.attrib = attrib;
}

int init_vmcb(__vcpu_t* vcpu, __vmm_context_t* vmm_context)
{
    __vmcb_control_area_t& control = vcpu->vmcb->control;
    __vmcb_save_state_t&   save    = vcpu->vmcb->save;

    control.intercept_misc_1 |= SVM_INTERCEPT_INVD
                              | SVM_INTERCEPT_MSR;

    control.intercept_misc_2 |= SVM_INTERCEPT_VMRUN   | SVM_INTERCEPT_VMMCALL
                              | SVM_INTERCEPT_VMLOAD  | SVM_INTERCEPT_VMSAVE
                              | SVM_INTERCEPT_STGI    | SVM_INTERCEPT_CLGI
                              | SVM_INTERCEPT_SKINIT;
	// Re-add SVM_INTERCEPT_WBINVD once we have a real handler for it. Windows
	// hammers WBINVD in the idle loop, so intercepting it right now means
	// constant VMEXITs and pegged CPU when the guest is idle.
    control.intercept_exceptions |= (1 << 1);

    control.msrpm_base_pa = vmm_context->msr_bitmap_pa;

    control.guest_asid = 1;

    control.tlb_control = 0;

    __pseudo_descriptor_64_t gdtr = {};
    _sgdt(&gdtr);

    fill_segment(save.cs, __read_cs(), gdtr);
    fill_segment(save.ss, __read_ss(), gdtr);
    fill_segment(save.ds, __read_ds(), gdtr);
    fill_segment(save.es, __read_es(), gdtr);
    fill_segment(save.fs, __read_fs(), gdtr);
    fill_segment(save.gs, __read_gs(), gdtr);
    fill_segment(save.tr, __read_tr(), gdtr);
    fill_segment(save.ldtr, __read_ldtr(), gdtr);

    __pseudo_descriptor_64_t idtr = {};
    _sidt(&idtr);
    save.gdtr.base = gdtr.base;
    save.gdtr.limit = gdtr.limit;
    save.idtr.base = idtr.base;
    save.idtr.limit = idtr.limit;

    save.cr2 = 0;
    save.cr0 = __readcr0();
    save.cr3 = __readcr3();
    save.cr4 = __readcr4();
    save.dr6 = __readdr(6);
    save.dr7 = __readdr(7);

    // shadow_efer might not be initialized; read from hardware directly.
    save.efer = __readmsr(MSR_EFER);
    save.rflags = __readeflags();

    save.rip = reinterpret_cast<uint64_t>(&guest_launched);

    // SYSCALL/SYSENTER MSRs. Skip these and the guest's first syscall after
    // VMRUN lands on a garbage RIP and instantly faults.
    save.star       = __readmsr(MSR_STAR);
    save.lstar      = __readmsr(MSR_LSTAR);
    save.cstar      = __readmsr(MSR_CSTAR);
    save.sfmask     = __readmsr(MSR_SFMASK);
    save.kernel_gs_base = __readmsr(MSR_SHADOW_GS_BASE);
    save.sysenter_cs  = __readmsr(MSR_SYSENTER_CS);
    save.sysenter_eip = __readmsr(MSR_SYSENTER_EIP);
    save.sysenter_esp = __readmsr(MSR_SYSENTER_ESP);
    save.pat = __readmsr(MSR_PAT);

    if (!NT_SUCCESS(npt_attach_global(vcpu))) {
        return FALSE;
    }
    return TRUE;
}
