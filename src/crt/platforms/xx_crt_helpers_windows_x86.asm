; Copyright (c) 2026 hors<horsicq@gmail.com>
; SPDX-License-Identifier: MIT
;
; MSVC x86 compiler helpers for a link without the Microsoft C runtime.
;
; The 32-bit compiler calls these for what it does not expand inline:
;
;   __chkstk, __alloca_probe      stack probe; EAX = bytes to allocate
;   __allmul                      64-bit multiply          (stack, callee pops)
;   __alldiv, __allrem            signed divide, remainder (stack, callee pops)
;   __aulldiv, __aullrem          unsigned                 (stack, callee pops)
;   __alldvrm, __aulldvrm         both: quotient EDX:EAX, remainder EBX:ECX
;   __allshl, __allshr, __aullshr EDX:EAX shifted by CL
;   __dtol3, __dtoul3, __dtoui3   double in XMM0 -> EDX:EAX (EAX for ui)
;   __dtoul3_legacy, __ftoul3_legacy  the names current compilers call for
;                                 the unsigned 64-bit conversions
;   __ftol3, __ftoul3, __ftoui3   float in XMM0  -> EDX:EAX (EAX for ui)
;   __ltod3, __ultod3             64-bit integer in EDX:ECX -> double in XMM0
;   __ltof3, __ultof3             64-bit integer in EDX:ECX -> float in XMM0
;   __tls_array                   offset of ThreadLocalStoragePointer in the TEB
;
; Every routine keeps all registers except the ones that carry its result
; (and, for the stack-argument ones, ECX). The conversions go through the x87
; unit with the control word set for the call, so they do not depend on the
; caller's precision or rounding setting: integer to floating point rounds
; once, to nearest; floating point to integer truncates.
;
; This is a clean implementation of the compiler-helper ABI; it does not
; contain or depend on Microsoft CRT code.

.686
.XMM
.MODEL FLAT
OPTION CASEMAP:NONE

PUBLIC __tls_array
__tls_array EQU 2Ch

_TEXT$xx SEGMENT ALIGN(16) 'CODE'

PUBLIC __chkstk
PUBLIC __alloca_probe
PUBLIC __allmul
PUBLIC __alldiv
PUBLIC __allrem
PUBLIC __aulldiv
PUBLIC __aullrem
PUBLIC __alldvrm
PUBLIC __aulldvrm
PUBLIC __allshl
PUBLIC __allshr
PUBLIC __aullshr
PUBLIC __dtol3
PUBLIC __dtoul3
PUBLIC __dtoul3_legacy
PUBLIC __dtoui3
PUBLIC __ftol3
PUBLIC __ftoul3
PUBLIC __ftoul3_legacy
PUBLIC __ftoui3
PUBLIC __ltod3
PUBLIC __ultod3
PUBLIC __ltof3
PUBLIC __ultof3

; ---------------------------------------------------------------- stack probe
; EAX = size. Touches every page from the caller's stack pointer down to the
; new one, then returns with ESP lowered by EAX. ECX is preserved.
ALIGN 16
__chkstk PROC
__alloca_probe LABEL NEAR
    push    ecx
    lea     ecx, [esp + 8]          ; the caller's ESP after this call returns
chk_page:
    cmp     eax, 1000h
    jb      chk_last
    sub     ecx, 1000h
    test    dword ptr [ecx], eax
    sub     eax, 1000h
    jmp     chk_page
chk_last:
    sub     ecx, eax
    test    dword ptr [ecx], eax
    mov     eax, esp                ; -> saved ECX, return address
    mov     esp, ecx
    mov     ecx, dword ptr [eax]
    mov     eax, dword ptr [eax + 4]
    push    eax
    ret
__chkstk ENDP

; ------------------------------------------------------------------- multiply
ALIGN 16
__allmul PROC
    push    ebx
    mov     eax, dword ptr [esp + 12]   ; a high
    mul     dword ptr [esp + 16]        ; * b low
    mov     ebx, eax
    mov     eax, dword ptr [esp + 8]    ; a low
    mul     dword ptr [esp + 20]        ; * b high
    add     ebx, eax
    mov     eax, dword ptr [esp + 8]    ; a low
    mul     dword ptr [esp + 16]        ; * b low
    add     edx, ebx
    pop     ebx
    ret     16
__allmul ENDP

; --------------------------------------------------------------------- divide
; In:  EDX:EAX dividend, ECX:EBX divisor (unsigned).
; Out: EDX:EAX quotient, ECX:EBX remainder. ESI, EDI, EBP are preserved.
; A zero divisor raises the divide error of the DIV instruction.
ALIGN 16
xx_udivmod PROC PRIVATE
    test    ecx, ecx
    jnz     udm_wide
    push    esi
    mov     esi, eax                ; dividend low
    mov     eax, edx
    xor     edx, edx
    div     ebx                     ; high quotient, remainder in EDX
    xchg    eax, esi                ; EAX = dividend low, ESI = high quotient
    div     ebx                     ; low quotient, remainder in EDX
    mov     ebx, edx
    mov     edx, esi
    pop     esi
    ret                             ; ECX is already zero
udm_wide:
    ; The divisor is at least 2^32, so the quotient has at most 32 bits:
    ; 32 steps of shift and subtract.
    push    ebp
    push    esi
    push    edi
    mov     esi, eax                ; dividend low: the bits still to bring down
    mov     edi, edx                ; remainder low
    xor     ebp, ebp                ; remainder high
    xor     eax, eax                ; quotient
    mov     edx, 32
udm_step:
    shl     esi, 1
    rcl     edi, 1
    rcl     ebp, 1
    jc      udm_subtract            ; the remainder passed 2^64
    cmp     ebp, ecx
    ja      udm_subtract
    jb      udm_keep
    cmp     edi, ebx
    jb      udm_keep
udm_subtract:
    sub     edi, ebx
    sbb     ebp, ecx
    lea     eax, [eax + eax + 1]
    jmp     udm_next
udm_keep:
    add     eax, eax
udm_next:
    dec     edx
    jnz     udm_step
    mov     ebx, edi
    mov     ecx, ebp
    xor     edx, edx
    pop     edi
    pop     esi
    pop     ebp
    ret
xx_udivmod ENDP

; The same for signed operands: the quotient truncates toward zero and the
; remainder has the sign of the dividend.
ALIGN 16
xx_sdivmod PROC PRIVATE
    push    edi
    xor     edi, edi
    test    edx, edx
    jns     sdm_divisor
    neg     edx
    neg     eax
    sbb     edx, 0
    mov     edi, 3                  ; bit 0: negate quotient, bit 1: remainder
sdm_divisor:
    test    ecx, ecx
    jns     sdm_divide
    neg     ecx
    neg     ebx
    sbb     ecx, 0
    xor     edi, 1
sdm_divide:
    call    xx_udivmod
    test    edi, 1
    jz      sdm_remainder
    neg     edx
    neg     eax
    sbb     edx, 0
sdm_remainder:
    test    edi, 2
    jz      sdm_done
    neg     ecx
    neg     ebx
    sbb     ecx, 0
sdm_done:
    pop     edi
    ret
xx_sdivmod ENDP

ALIGN 16
__aulldiv PROC
    push    ebx
    mov     eax, dword ptr [esp + 8]
    mov     edx, dword ptr [esp + 12]
    mov     ebx, dword ptr [esp + 16]
    mov     ecx, dword ptr [esp + 20]
    call    xx_udivmod
    pop     ebx
    ret     16
__aulldiv ENDP

ALIGN 16
__aullrem PROC
    push    ebx
    mov     eax, dword ptr [esp + 8]
    mov     edx, dword ptr [esp + 12]
    mov     ebx, dword ptr [esp + 16]
    mov     ecx, dword ptr [esp + 20]
    call    xx_udivmod
    mov     eax, ebx
    mov     edx, ecx
    pop     ebx
    ret     16
__aullrem ENDP

ALIGN 16
__aulldvrm PROC
    mov     eax, dword ptr [esp + 4]
    mov     edx, dword ptr [esp + 8]
    mov     ebx, dword ptr [esp + 12]
    mov     ecx, dword ptr [esp + 16]
    call    xx_udivmod
    xchg    ebx, ecx                ; remainder in EBX:ECX
    ret     16
__aulldvrm ENDP

ALIGN 16
__alldiv PROC
    push    ebx
    mov     eax, dword ptr [esp + 8]
    mov     edx, dword ptr [esp + 12]
    mov     ebx, dword ptr [esp + 16]
    mov     ecx, dword ptr [esp + 20]
    call    xx_sdivmod
    pop     ebx
    ret     16
__alldiv ENDP

ALIGN 16
__allrem PROC
    push    ebx
    mov     eax, dword ptr [esp + 8]
    mov     edx, dword ptr [esp + 12]
    mov     ebx, dword ptr [esp + 16]
    mov     ecx, dword ptr [esp + 20]
    call    xx_sdivmod
    mov     eax, ebx
    mov     edx, ecx
    pop     ebx
    ret     16
__allrem ENDP

ALIGN 16
__alldvrm PROC
    mov     eax, dword ptr [esp + 4]
    mov     edx, dword ptr [esp + 8]
    mov     ebx, dword ptr [esp + 12]
    mov     ecx, dword ptr [esp + 16]
    call    xx_sdivmod
    xchg    ebx, ecx                ; remainder in EBX:ECX
    ret     16
__alldvrm ENDP

; --------------------------------------------------------------------- shifts
; EDX:EAX by CL; a count of 64 or more gives 0 (or the sign for __allshr).
ALIGN 16
__allshl PROC
    cmp     cl, 64
    jae     shl_all
    cmp     cl, 32
    jae     shl_word
    shld    edx, eax, cl
    shl     eax, cl
    ret
shl_word:
    mov     edx, eax
    xor     eax, eax
    shl     edx, cl                 ; the processor uses CL mod 32
    ret
shl_all:
    xor     eax, eax
    xor     edx, edx
    ret
__allshl ENDP

ALIGN 16
__aullshr PROC
    cmp     cl, 64
    jae     shr_all
    cmp     cl, 32
    jae     shr_word
    shrd    eax, edx, cl
    shr     edx, cl
    ret
shr_word:
    mov     eax, edx
    xor     edx, edx
    shr     eax, cl
    ret
shr_all:
    xor     eax, eax
    xor     edx, edx
    ret
__aullshr ENDP

ALIGN 16
__allshr PROC
    cmp     cl, 64
    jae     sar_all
    cmp     cl, 32
    jae     sar_word
    shrd    eax, edx, cl
    sar     edx, cl
    ret
sar_word:
    mov     eax, edx
    sar     edx, 31
    sar     eax, cl
    ret
sar_all:
    sar     edx, 31
    mov     eax, edx
    ret
__allshr ENDP

; ---------------------------------------------------------------- conversions
; Control words: all exceptions masked, 64-bit precision; nearest or truncate.
CW_NEAREST  EQU 037Fh
CW_TRUNCATE EQU 0F7Fh

; ST(0) -> signed 64-bit integer in EDX:EAX, truncated. ST(0) is popped.
ALIGN 16
xx_st0_to_i64 PROC PRIVATE
    sub     esp, 12
    fnstcw  word ptr [esp + 8]
    mov     word ptr [esp + 10], CW_TRUNCATE
    fldcw   word ptr [esp + 10]
    fistp   qword ptr [esp]
    fldcw   word ptr [esp + 8]
    mov     eax, dword ptr [esp]
    mov     edx, dword ptr [esp + 4]
    add     esp, 12
    ret
xx_st0_to_i64 ENDP

; ST(0) -> unsigned 64-bit integer in EDX:EAX, truncated. Values of 2^63 and
; more are converted after 2^63 is taken off.
ALIGN 16
xx_st0_to_u64 PROC PRIVATE
    fld     dword ptr [two63]
    fcomip  st(0), st(1)            ; 2^63 against the value
    ja      xx_st0_to_i64           ; the value is below 2^63
    jp      xx_st0_to_i64           ; not a number
    fsub    dword ptr [two63]
    call    xx_st0_to_i64
    add     edx, 80000000h
    ret
xx_st0_to_u64 ENDP

ALIGN 16
__dtol3 PROC
    sub     esp, 8
    movsd   qword ptr [esp], xmm0
    fld     qword ptr [esp]
    add     esp, 8
    jmp     xx_st0_to_i64
__dtol3 ENDP

ALIGN 16
__dtoul3 PROC
__dtoul3_legacy LABEL NEAR
    sub     esp, 8
    movsd   qword ptr [esp], xmm0
    fld     qword ptr [esp]
    add     esp, 8
    jmp     xx_st0_to_u64
__dtoul3 ENDP

ALIGN 16
__dtoui3 PROC
    push    edx
    sub     esp, 8
    movsd   qword ptr [esp], xmm0
    fld     qword ptr [esp]
    add     esp, 8
    call    xx_st0_to_i64
    pop     edx
    ret
__dtoui3 ENDP

ALIGN 16
__ftol3 PROC
    sub     esp, 4
    movss   dword ptr [esp], xmm0
    fld     dword ptr [esp]
    add     esp, 4
    jmp     xx_st0_to_i64
__ftol3 ENDP

ALIGN 16
__ftoul3 PROC
__ftoul3_legacy LABEL NEAR
    sub     esp, 4
    movss   dword ptr [esp], xmm0
    fld     dword ptr [esp]
    add     esp, 4
    jmp     xx_st0_to_u64
__ftoul3 ENDP

ALIGN 16
__ftoui3 PROC
    push    edx
    sub     esp, 4
    movss   dword ptr [esp], xmm0
    fld     dword ptr [esp]
    add     esp, 4
    call    xx_st0_to_i64
    pop     edx
    ret
__ftoui3 ENDP

; Integer in EDX:ECX to floating point. The integer is loaded exactly (it fits
; the 64-bit significand) and rounded once when it is stored.
; Frame: [esp] the integer, [esp+8] saved control word, [esp+10] working
; control word, [esp+12] the result.
ALIGN 16
__ltod3 PROC
    sub     esp, 20
    mov     dword ptr [esp], ecx
    mov     dword ptr [esp + 4], edx
    fnstcw  word ptr [esp + 8]
    mov     word ptr [esp + 10], CW_NEAREST
    fldcw   word ptr [esp + 10]
    fild    qword ptr [esp]
    fstp    qword ptr [esp + 12]
    fldcw   word ptr [esp + 8]
    movsd   xmm0, qword ptr [esp + 12]
    add     esp, 20
    ret
__ltod3 ENDP

ALIGN 16
__ultod3 PROC
    sub     esp, 20
    mov     dword ptr [esp], ecx
    mov     dword ptr [esp + 4], edx
    fnstcw  word ptr [esp + 8]
    mov     word ptr [esp + 10], CW_NEAREST
    fldcw   word ptr [esp + 10]
    fild    qword ptr [esp]
    test    edx, edx
    jns     ultod_store
    fadd    dword ptr [two64]       ; exact in the 64-bit significand
ultod_store:
    fstp    qword ptr [esp + 12]
    fldcw   word ptr [esp + 8]
    movsd   xmm0, qword ptr [esp + 12]
    add     esp, 20
    ret
__ultod3 ENDP

ALIGN 16
__ltof3 PROC
    sub     esp, 20
    mov     dword ptr [esp], ecx
    mov     dword ptr [esp + 4], edx
    fnstcw  word ptr [esp + 8]
    mov     word ptr [esp + 10], CW_NEAREST
    fldcw   word ptr [esp + 10]
    fild    qword ptr [esp]
    fstp    dword ptr [esp + 12]
    fldcw   word ptr [esp + 8]
    movss   xmm0, dword ptr [esp + 12]
    add     esp, 20
    ret
__ltof3 ENDP

ALIGN 16
__ultof3 PROC
    sub     esp, 20
    mov     dword ptr [esp], ecx
    mov     dword ptr [esp + 4], edx
    fnstcw  word ptr [esp + 8]
    mov     word ptr [esp + 10], CW_NEAREST
    fldcw   word ptr [esp + 10]
    fild    qword ptr [esp]
    test    edx, edx
    jns     ultof_store
    fadd    dword ptr [two64]
ultof_store:
    fstp    dword ptr [esp + 12]
    fldcw   word ptr [esp + 8]
    movss   xmm0, dword ptr [esp + 12]
    add     esp, 20
    ret
__ultof3 ENDP

ALIGN 4
two63   DD 5F000000h                ; 2^63 as a float
two64   DD 5F800000h                ; 2^64 as a float

_TEXT$xx ENDS

END
