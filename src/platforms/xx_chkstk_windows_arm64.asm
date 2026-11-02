; Copyright (c) 2026 hors<horsicq@gmail.com>
; SPDX-License-Identifier: MIT
;
; MSVC ARM64 compiler helpers for a CRT-free static archive.
;
; __chkstk follows the Windows ARM64 helper ABI: X15 contains the stack
; allocation in 16-byte units, X16 and X17 are scratch, and SP is unchanged.
; The interlocked helpers operate on 32-bit Windows LONG values and preserve
; the acquire/release ordering of their MSVC intrinsic counterparts.

        AREA |.text$mn$00|, CODE, READONLY, ALIGN=2

        EXPORT __chkstk
        EXPORT _InterlockedCompareExchange
        EXPORT _InterlockedExchange
        EXPORT _InterlockedIncrement

        ALIGN 2
__chkstk
        ldr     x17, [x18, #0x10]       ; NT_TIB.StackLimit
        subs    x16, sp, x15, lsl #4    ; caller's target SP
        csel    x16, xzr, x16, lo       ; clamp address underflow
        cmp     x16, x17
        b.hs    xx_chkstk_done
        and     x16, x16, #-4096        ; page containing the target
xx_chkstk_probe
        sub     x17, x17, #1, lsl #12
        ldr     xzr, [x17]
        cmp     x17, x16
        b.ne    xx_chkstk_probe
xx_chkstk_done
        ret

        ALIGN 2
_InterlockedCompareExchange
xx_compare_exchange_retry
        ldaxr   w3, [x0]
        cmp     w3, w2
        b.ne    xx_compare_exchange_mismatch
        stlxr   w4, w1, [x0]
        cbnz    w4, xx_compare_exchange_retry
        mov     w0, w3
        ret
xx_compare_exchange_mismatch
        clrex
        mov     w0, w3
        ret

        ALIGN 2
_InterlockedExchange
xx_exchange_retry
        ldaxr   w2, [x0]
        stlxr   w3, w1, [x0]
        cbnz    w3, xx_exchange_retry
        mov     w0, w2
        ret

        ALIGN 2
_InterlockedIncrement
xx_increment_retry
        ldaxr   w1, [x0]
        add     w1, w1, #1
        stlxr   w2, w1, [x0]
        cbnz    w2, xx_increment_retry
        mov     w0, w1
        ret

        END
