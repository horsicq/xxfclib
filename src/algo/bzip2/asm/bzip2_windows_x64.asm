; Copyright (c) 2026 hors<horsicq@gmail.com>
; SPDX-License-Identifier: MIT
;
; MSVC x64 BZip2 Decompressor Acceleration Engine

_TEXT SEGMENT

; bool xx_bzip2_bwt_traverse_x64(
;     const uint32_t *tt,      ; RCX
;     int tt_count,            ; EDX
;     uint32_t orig_ptr,       ; R8D
;     uint8_t *dest,           ; R9
;     size_t dest_cap,         ; [RSP + 40]
;     size_t *out_written      ; [RSP + 48]
; );

PUBLIC xx_bzip2_bwt_traverse_x64
align 16
xx_bzip2_bwt_traverse_x64 PROC FRAME
    push    rbx
    .pushreg rbx
    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    push    r12
    .pushreg r12
    push    r13
    .pushreg r13
    push    r14
    .pushreg r14
    push    r15
    .pushreg r15
    .endprolog

    mov     r10, [rsp + 56 + 40]    ; dest_cap
    mov     r15, [rsp + 56 + 48]    ; out_written pointer

    test    edx, edx
    jle     @@fail
    cmp     r8d, edx
    jae     @@fail
    test    r9, r9
    jz      @@fail
    mov     r8d, r8d                ; the ABI leaves bits 32-63 of a uint32_t undefined

    mov     rsi, rcx                ; rsi = tt
    mov     rdi, r9                 ; rdi = dest (cursor)
    mov     rbx, r9                 ; rbx = dest (base)
    lea     r13, [r9 + r10]         ; r13 = dest_end (limit)
    mov     r11d, edx               ; r11d = tt_count (loop counter)

    mov     r14d, [rsi + r8*4]      ; t = tt[orig_ptr]

    xor     r8d, r8d                ; rle_count = 0
    mov     r9d, 100h               ; prev_byte = 0x100

align 16
@@bwt_loop:
    movzx   r12d, r14b              ; ob = t & 0xFF
    shr     r14d, 8                 ; index = t >> 8
    mov     r14d, [rsi + r14*4]     ; t = tt[index]

    cmp     r8d, 4
    je      @@is_rle4

    cmp     r12d, r9d
    jne     @@diff_byte
    inc     r8d                     ; same byte: rle_count++
    cmp     rdi, r13
    jae     @@fail
    mov     [rdi], r12b
    inc     rdi
    dec     r11d
    jnz     @@bwt_loop
    jmp     @@success

align 16
@@diff_byte:
    mov     r9d, r12d               ; prev_byte = ob
    mov     r8d, 1                  ; rle_count = 1
    cmp     rdi, r13
    jae     @@fail
    mov     [rdi], r12b
    inc     rdi
    dec     r11d
    jnz     @@bwt_loop
    jmp     @@success

align 16
@@is_rle4:
    lea     rax, [rdi + r12]
    cmp     rax, r13
    ja      @@fail

    test    r12d, r12d
    jz      @@rle_fill_done
    mov     ecx, r12d
    mov     al, r9b
@@fill_loop:
    mov     [rdi], al
    inc     rdi
    dec     ecx
    jnz     @@fill_loop

@@rle_fill_done:
    xor     r8d, r8d                ; rle_count = 0
    mov     r9d, 100h               ; prev_byte = 0x100
    dec     r11d
    jnz     @@bwt_loop

@@success:
    sub     rdi, rbx
    test    r15, r15
    jz      @@ret_true
    mov     [r15], rdi
@@ret_true:
    mov     eax, 1
    jmp     @@cleanup

@@fail:
    xor     eax, eax

@@cleanup:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret
xx_bzip2_bwt_traverse_x64 ENDP

; void xx_bzip2_bwt_build_x64(
;     const uint8_t *bwt_bytes,  ; RCX
;     uint32_t *tt,              ; RDX
;     const uint32_t *freq,      ; R8
;     int tt_count               ; R9D
; );

PUBLIC xx_bzip2_bwt_build_x64
align 16
xx_bzip2_bwt_build_x64 PROC FRAME
    push    rbx
    .pushreg rbx
    push    rsi
    .pushreg rsi
    push    rdi
    .pushreg rdi
    push    r12
    .pushreg r12
    sub     rsp, 1024 + 8
    .allocstack 1024 + 8
    .endprolog

    mov     rsi, rcx               ; bwt_bytes
    mov     rdi, rdx               ; tt
    mov     r10, r8                ; freq
    mov     r11d, r9d              ; tt_count

    mov     dword ptr [rsp], 0
    xor     ecx, ecx
    xor     edx, edx
@@cum_loop:
    mov     eax, [r10 + rcx*4]
    add     edx, eax
    inc     ecx
    mov     [rsp + rcx*4], edx
    cmp     ecx, 255
    jl      @@cum_loop

    xor     ecx, ecx
    test    r11d, r11d
    jle     @@build_done

align 16
@@scatter_loop:
    movzx   eax, byte ptr [rsi + rcx]
    mov     edx, [rsp + rax*4]
    inc     dword ptr [rsp + rax*4]
    mov     r8d, ecx
    shl     r8d, 8
    or      r8d, eax
    mov     [rdi + rdx*4], r8d

    inc     ecx
    cmp     ecx, r11d
    jl      @@scatter_loop

@@build_done:
    add     rsp, 1024 + 8
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret
xx_bzip2_bwt_build_x64 ENDP

_TEXT ENDS
END
