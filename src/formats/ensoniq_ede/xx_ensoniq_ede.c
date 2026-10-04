/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * EDE/EDA/EDS/EDT/EDV facts: HxC ede_loader and its bundled format description.
 * MSB-first sparse bitmap bits encode the alternating 6D B6 fill. Types1/2
 * use 1024 stored bytes per present sector; sectorID5 retains its first512.
 * The exact stored length is checked. Unknown codes/layouts are rejected.
 */
#include "xxfclib/formats/ensoniq_ede/xx_ensoniq_ede.h"
#include "../xx_hxc_sector.h"
static bool ede_parse(Abstractformat *f, pm_stream *s, hc_blob *b) {
    xx_ensoniq_ede *r = (xx_ensoniq_ede *)f;
    uint32_t heads = 2U, sectors = 10U, stride = 512U, bitmap = 160U, total, stored = 512U;
    uint32_t track, k, ordinal = 0U, per_track, c, h, code; uint8_t *out; bool mixed = false;
    if (!hc_span_ok(b, 0U, 512U) || b->p[0] != 13U || b->p[1] != 10U ||
        b->p[78] != 13U || b->p[79] != 10U) return false;
    code = b->p[511];
    switch (code) {
    case 0U: case 3U: case 4U: case 7U: break;
    case 1U: heads = 1U; sectors = 6U; stride = 1024U; mixed = true; break;
    case 2U: sectors = 6U; stride = 1024U; mixed = true; break;
    case 203U: case 204U: sectors = 20U; bitmap = 96U; break;
    default: return false;
    }
    if (b->p[bitmap - 3U] != 13U || b->p[bitmap - 2U] != 10U || b->p[bitmap - 1U] != 26U ||
        /* Computer-sized images use a different geometry which is not
         * established by the sparse bitmap alone. Do not invent sectors. */
        b->p[510] != 0U) return false;
    per_track = mixed ? 5632U : sectors * stride;
    total = 80U * heads * per_track;
    if (bitmap + (80U * heads * sectors + 7U) / 8U > 510U) return false;
    for (k = 0U; k < 80U * heads * sectors; ++k)
        if (!(b->p[bitmap + k / 8U] & (0x80U >> (k % 8U)))) {
            if (!hc_span_ok(b, stored, stride)) return false;
            stored += stride;
        }
    if (stored != b->n) return false;
    out = hc_alloc(b, total); if (!out) return false;
    stored = 512U;
    for (c = 0U; c < 80U; ++c) for (h = 0U; h < heads; ++h) {
        track = (c * heads + h) * per_track;
        for (k = 0U; k < sectors; ++k, ++ordinal) {
            uint32_t bytes = mixed && !k ? 512U : stride;
            uint32_t destination = track + (mixed ? (!k ? 5120U : (k - 1U) * 1024U) : k * stride), j;
            if (!hc_poll(b)) { xx_mem_free(out); return false; }
            if (b->p[bitmap + ordinal / 8U] & (0x80U >> (ordinal % 8U))) {
                for (j = 0U; j < bytes; ++j) out[destination + j] = (j & 1U) ? 0xB6U : 0x6DU;
            } else { xx_rt_memcpy(out + destination, b->p + stored, bytes); stored += stride; }
        }
    }
    r->cylinders = 80U; r->heads = heads; r->sectors_per_track = sectors; r->sector_size = mixed ? 0U : stride;
    r->note = mixed ? "sectorIDs0..4=1024 bytes, ID5=512; bitmap fill decoded" : "sparse bitmap fill decoded; raw sectors";
    return hc_memory(f, s, b, "disk.img", out, total);
}
HC_PARSE_WRAPPER(ede_parse)
HC_DEFINE_READER(ensoniq_ede, XX_FILE_TYPE_ENSONIQ_EDE, "ede")
