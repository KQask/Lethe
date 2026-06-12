#pragma once

#ifdef __cplusplus
extern "C" {
#endif

    // rcx = stack : __vmm_stack_t* at base of VMM stack (RSP & ~(VMM_STACK_SIZE-1))
    // rdx = regs  : __guest_registers_t* pointing to the pushed GPRs (RSP after pushes)
    void vmexit_handler(struct __vmm_stack_t* stack, struct __guest_registers_t* regs);

#ifdef __cplusplus
}
#endif
