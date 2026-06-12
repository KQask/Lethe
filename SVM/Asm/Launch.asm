.code

EXTERN vmexit_entrypoint : PROC

PUBLIC svm_launch
PUBLIC guest_launched

; svm_launch(vmcb_pa, &vmcb.save.rsp, host_stack_top, host_save_area_pa)
svm_launch proc
    mov [rdx], rsp
    mov rsp, r8

    mov rax, r9
    db 0Fh, 01h, 0DBh       ; VMSAVE [host_save_area_pa]

    mov rax, rcx
    vmrun rax

    jmp vmexit_entrypoint

    xor al, al
    ret
svm_launch endp

guest_launched proc
    mov al, 1
    ret
guest_launched endp

end
