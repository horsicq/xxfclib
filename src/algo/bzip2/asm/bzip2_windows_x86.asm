; Copyright (c) 2026 hors<horsicq@gmail.com>
; SPDX-License-Identifier: MIT
;
; MSVC x86 (32-bit) BZip2 Decompressor Acceleration Engine
;
; MASM addresses PROC parameters and LOCALs through EBP, so EBP is never used
; for data here. x86 has no BPL: the output byte lives in EBX (BL).

.386
.MODEL FLAT, C
OPTION CASEMAP:NONE

_TEXT SEGMENT

PUBLIC xx_bzip2_bwt_traverse_x86
xx_bzip2_bwt_traverse_x86 PROC C USES ebx esi edi, \
    tt:DWORD, \
    tt_count:DWORD, \
    orig_ptr:DWORD, \
    dest:DWORD, \
    dest_cap:DWORD, \
    out_written:DWORD

    LOCAL dest_end:DWORD

    mov     edx, tt_count
    test    edx, edx
    jle     @@fail
    mov     eax, orig_ptr
    cmp     eax, edx
    jae     @@fail
    mov     edi, dest
    test    edi, edi
    jz      @@fail
    mov     ecx, dest_cap
    add     ecx, edi
    jc      @@fail
    mov     dest_end, ecx           ; dest_end = dest + dest_cap (limit)

    mov     esi, tt
    mov     eax, [esi + eax*4]      ; t = tt[orig_ptr]

    xor     edx, edx                ; rle_count = 0
    mov     ecx, 100h               ; prev_byte = 0x100

@@loop:
    movzx   ebx, al                 ; ob = t & 0xFF
    shr     eax, 8                  ; index = t >> 8
    mov     eax, [esi + eax*4]      ; t = tt[index]

    cmp     edx, 4
    je      @@is_rle4

    cmp     edi, dest_end
    jae     @@fail
    cmp     ebx, ecx
    jne     @@diff_byte
    inc     edx                     ; rle_count++
    jmp     @@store_byte

@@diff_byte:
    mov     ecx, ebx                ; prev_byte = ob
    mov     edx, 1                  ; rle_count = 1

@@store_byte:
    mov     [edi], bl
    inc     edi
    dec     tt_count
    jnz     @@loop
    jmp     @@success

@@is_rle4:
    lea     edx, [edi + ebx]        ; edx is reset below, use it as the end
    cmp     edx, dest_end
    ja      @@fail
    test    ebx, ebx
    jz      @@rle_fill_done
@@fill_loop:
    mov     [edi], cl               ; prev_byte (< 0x100 after four equal bytes)
    inc     edi
    dec     ebx
    jnz     @@fill_loop

@@rle_fill_done:
    xor     edx, edx                ; rle_count = 0
    mov     ecx, 100h               ; prev_byte = 0x100
    dec     tt_count
    jnz     @@loop

@@success:
    sub     edi, dest
    mov     eax, out_written
    test    eax, eax
    jz      @@ret_true
    mov     [eax], edi
@@ret_true:
    mov     eax, 1
    ret

@@fail:
    xor     eax, eax
    ret
xx_bzip2_bwt_traverse_x86 ENDP

PUBLIC xx_bzip2_bwt_build_x86
xx_bzip2_bwt_build_x86 PROC C USES ebx esi edi, \
    bwt_bytes:DWORD, \
    tt:DWORD, \
    freq:DWORD, \
    tt_count:DWORD

    LOCAL cum[256]:DWORD

    mov     cum[0], 0
    mov     esi, freq
    xor     ecx, ecx
    xor     edx, edx
@@cum_loop:
    mov     eax, [esi + ecx*4]
    add     edx, eax
    inc     ecx
    mov     cum[ecx*4], edx
    cmp     ecx, 255
    jl      @@cum_loop

    mov     esi, bwt_bytes
    mov     edi, tt
    xor     ecx, ecx
    cmp     ecx, tt_count
    jge     @@done

@@scatter_loop:
    movzx   eax, byte ptr [esi + ecx]
    mov     edx, cum[eax*4]
    inc     cum[eax*4]
    mov     ebx, ecx
    shl     ebx, 8
    or      ebx, eax
    mov     [edi + edx*4], ebx

    inc     ecx
    cmp     ecx, tt_count
    jl      @@scatter_loop

@@done:
    ret
xx_bzip2_bwt_build_x86 ENDP

_TEXT ENDS
END
