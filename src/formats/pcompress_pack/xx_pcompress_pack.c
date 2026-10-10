/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * PCompress PACK name/newline/BE-length framing and LH compressed blocks.
 * Reference: libxad Zoom.c PCompPACK/DoDecrunch wire-format facts. */
#include "xxfclib/formats/pcompress_pack/xx_pcompress_pack.h"
#include "../xx_legacy_archive.h"
#include "../xx_legacy_huffman.h"
static bool pcompress_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t at = 4;
    if (b->n < 10U || xx_rt_memcmp(b->p, "PACK", 4)) return false;
    while (at < b->n) {
        uint32_t start = at, size, n, packed, crc;
        char name[96];
        uint8_t *out;
        if (!ac_poll(b)) return false;
        while (at < b->n && b->p[at] != '\n') {
            if ((b->p[at] & 0x7FU) < 32U || at - start >= 90U) return false;
            ++at;
        }
        if (!ac_span(b, at, 5U) || !ac_name(name, sizeof(name), b->p + start, at - start)) return false;
        size = xx_data_get_u32(b->p + at + 1U, 4, 0, true);
        at += 5U;
        if (!ac_span(b, at, size)) return false;
        if (size > 14U && b->p[at] == 'L' && b->p[at + 1] == 'H' && b->p[at + 2] == 0U && b->p[at + 6] == 0U) {
            uint32_t written = 0, used = 0;
            n = xx_data_get_u32(b->p + at + 2, 4, 0, true);
            packed = xx_data_get_u32(b->p + at + 6, 4, 0, true);
            crc = xx_data_get_u32(b->p + at + 10, 4, 0, true);
            if (packed != size - 14U) {
                return false;
            }
            out = ac_alloc(b, n);
            if (!out) return false;
            /* PCompress checks the packed bytes, then decodes its 317-symbol
             * Zoom LH alphabet with an explicit EOF (distinct from LH1). */
            if (ac_crc32(b->p + at + 14U, packed, 0U) != crc || !ac_adaptive(b, b->p + at + 14U, packed, out, n, &written, &used, 3U) || written != n || used != packed) {
                ac_release(b, out, n);
                return ac_error(b, "PCompress LH decode or CRC failed");
            }
            if (!ac_memory(f, s, b, name, out, n, size, 1)) return false;
        } else if (!ac_emit(f, s, b, name, at, size)) return false;
        at += size;
    }
    return at == b->n && s->count;
}
AC_PARSE(pcompress_parse)
AC_DEFINE(pcompress_pack, XX_FILE_TYPE_PCOMPRESS_PACK, "pack")
