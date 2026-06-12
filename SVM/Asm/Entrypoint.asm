.code

PUBLIC __read_cs
PUBLIC __read_ss
PUBLIC __read_ds
PUBLIC __read_es
PUBLIC __read_fs
PUBLIC __read_gs
PUBLIC __read_ldtr
PUBLIC __read_tr
PUBLIC __load_ar
PUBLIC _sgdt
PUBLIC _sidt
PUBLIC hv2_vmmcall
PUBLIC vmexit_entrypoint

__read_cs proc
    mov ax, cs
    ret
__read_cs endp

__read_ss proc
    mov ax, ss
    ret
__read_ss endp

__read_ds proc
    mov ax, ds
    ret
__read_ds endp

__read_es proc
    mov ax, es
    ret
__read_es endp

__read_fs proc
    mov ax, fs
    ret
__read_fs endp

__read_gs proc
    mov ax, gs
    ret
__read_gs endp

__read_ldtr proc
    sldt ax
    ret
__read_ldtr endp

__read_tr proc
    str ax
    ret
__read_tr endp

__load_ar proc
    lar rax, rcx
    jz  @success
    xor rax, rax
@success:
    ret
__load_ar endp

_sgdt proc
    sgdt [rcx]
    ret
_sgdt endp

_sidt proc
    sidt [rcx]
    ret
_sidt endp

hv2_vmmcall PROC
    vmmcall
    ret
hv2_vmmcall ENDP


EXTERN vmexit_handler : PROC

vmexit_entrypoint proc

    vmrun_loop:
        mov rax, rsp
        and rax, NOT (8000h - 1)
        mov rax, [rax]
        mov rax, [rax + 8]          ; vcpu->vmcb_physical

        db 0Fh, 01h, 0DBh           ; VMSAVE [RAX]

        mov rax, rsp
        and rax, NOT (8000h - 1)
        mov rax, [rax]
        mov rax, [rax + 18h]        ; vcpu->host_save_area_pa
        db 0Fh, 01h, 0DAh           ; VMLOAD [RAX]

        push r15
        push r14
        push r13
        push r12
        push r11
        push r10
        push r9
        push r8
        push rbp
        push rdi
        push rsi
        push rdx
        push rcx
        push rbx

        mov rdx, rsp
        mov rcx, rsp
        and rcx, NOT (8000h - 1)

        sub rsp, 28h
        call vmexit_handler
        add rsp, 28h


        pop rbx
        pop rcx
        pop rdx
        pop rsi
        pop rdi
        pop rbp
        pop r8
        pop r9
        pop r10
        pop r11
        pop r12
        pop r13
        pop r14
        pop r15

        mov rax, rsp
        and rax, NOT (8000h - 1)
        mov rax, [rax]
        mov rax, [rax + 8]          ; vcpu->vmcb_physical

        db 0Fh, 01h, 0DAh           ; VMLOAD [RAX]
        db 0Fh, 01h, 0DCh           ; STGI
        sti
        vmrun rax
    jmp vmrun_loop
    ret

vmexit_entrypoint endp

end
