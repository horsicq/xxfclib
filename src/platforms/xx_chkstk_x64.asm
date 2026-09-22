; Copyright (c) 2026 hors<horsicq@gmail.com>
; SPDX-License-Identifier: MIT
;
; MSVC x64 stack-probe helper for the CRT-free shared build.
;
; ABI contract:
;   RAX = number of bytes the caller will subtract from RSP after return.
;   RAX, R10 and R11 are preserved; no other register is touched.
;   RSP is unchanged on return.
;
; The probe starts at the TEB StackLimit and commits one guard page at a time
; down to the page containing the caller's target stack pointer.  This is a
; clean implementation of the documented compiler-helper ABI; it does not
; contain or depend on Microsoft CRT code.

OPTION CASEMAP:NONE

_TEXT SEGMENT ALIGN(16) 'CODE'

PUBLIC __chkstk

ALIGN 16
__chkstk PROC FRAME
    sub     rsp, 10h
    .allocstack 10h
    .endprolog

    mov     qword ptr [rsp], r10
    mov     qword ptr [rsp + 8], r11

    xor     r11, r11
    lea     r10, [rsp + 18h]       ; caller RSP before CALL
    sub     r10, rax               ; target after caller's allocation
    cmovb   r10, r11               ; clamp address underflow to zero

    mov     r11, qword ptr gs:[10h] ; NT_TIB.StackLimit
    cmp     r10, r11
    jae     probe_done

    and     r10w, 0F000h           ; align target down to a 4 KiB page

probe_page:
    lea     r11, [r11 - 1000h]
    mov     byte ptr [r11], 0
    cmp     r10, r11
    jne     probe_page

probe_done:
    mov     r10, qword ptr [rsp]
    mov     r11, qword ptr [rsp + 8]
    add     rsp, 10h
    ret
__chkstk ENDP

_TEXT ENDS
END
