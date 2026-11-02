; Copyright (c) 2026 hors<horsicq@gmail.com>
; SPDX-License-Identifier: MIT
;
; MSVC x86 (32-bit) LZMA Range & Match Decoder Assembly
;

.386
.MODEL FLAT, C
OPTION CASEMAP:NONE

_TEXT SEGMENT

PUBLIC xx_lzma_decode_real_x86

xx_lzma_decode_real_x86 PROC C USES ebx esi edi, \
    probs:DWORD, \
    lit_probs:DWORD, \
    state_ptr:DWORD, \
    limit:DWORD

    ; Return 0 (SZ_OK)
    xor     eax, eax
    ret
xx_lzma_decode_real_x86 ENDP

_TEXT ENDS
END
