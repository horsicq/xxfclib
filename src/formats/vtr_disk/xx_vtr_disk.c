/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded VTrucco revision0 component reader. Format facts:
 * HxC vtr_format.h and writer, 512-byte units and interleaved 256-byte sides.
 */
#include "xxfclib/formats/vtr_disk/xx_vtr_disk.h"
#include "../xx_hxc_sector.h"
static uint8_t vtr_reverse(uint8_t x) {
    x = (uint8_t)((x >> 4U) | (x << 4U));
    x = (uint8_t)(((x & 0xCCU) >> 2U) | ((x & 0x33U) << 2U));
    return (uint8_t)(((x & 0xAAU) >> 1U) | ((x & 0x55U) << 1U));
}
static bool vtr_parse(Abstractformat *f, pm_stream *s, hc_blob *b) {
    xx_vtr_disk *r = (xx_vtr_disk *)f; hc_span spans[256]; uint32_t count = 0U;
    uint32_t table, table_bytes, c, h, at, len, stored, sidebytes, j; char name[64]; uint8_t *out;
    if (!hc_span_ok(b, 0U, 512U) || xx_rt_memcmp(b->p, "VTrucco", 7U) || b->p[7] > 1U ||
        !b->p[10] || !b->p[12] || b->p[12] > 2U || b->p[13] || b->p[18] ||
        !pm_le16(b->p + 14U) || pm_le16(b->p + 14U) > 1000U) return false;
    table = (uint32_t)pm_le16(b->p + 16U) * 512U;
    table_bytes = ((uint32_t)b->p[10] * 4U + 511U) & ~511U;
    if (table < 512U || !hc_span_ok(b, table, table_bytes) ||
        !hc_disjoint(spans, &count, 256U, 0U, table + table_bytes)) return false;
    /* Authenticate the complete table before interpreting any track data. */
    for (c = 0U; c < b->p[10]; ++c) {
        at = (uint32_t)pm_le16(b->p + table + c * 4U) * 512U;
        len = pm_le16(b->p + table + c * 4U + 2U);
        stored = (len + 511U) & ~511U;
        if (!len || (len & 1U) || !hc_span_ok(b, at, stored) ||
            !hc_disjoint(spans, &count, 256U, at, stored)) return false;
    }
    r->cylinders = b->p[10]; r->heads = b->p[12];
    r->note = "revision0 original track cells, deinterleaved and bit-order normalized; no filesystem decode";
    for (c = 0U; c < r->cylinders; ++c) {
        at = (uint32_t)pm_le16(b->p + table + c * 4U) * 512U;
        sidebytes = pm_le16(b->p + table + c * 4U + 2U) / 2U;
        for (h = 0U; h < r->heads; ++h) {
            out = hc_alloc(b, sidebytes); if (!out) return false;
            for (j = 0U; j < sidebytes; ++j) {
                if (!(j & 1023U) && !hc_poll(b)) { xx_mem_free(out); return false; }
                out[j] = vtr_reverse(b->p[at + (j / 256U) * 512U + h * 256U + j % 256U]);
            }
            xx_rt_snprintf(name, sizeof(name), "track-%03u-side-%u.bits", c, h);
            if (!hc_memory(f, s, b, name, out, sidebytes)) return false;
        }
    }
    return true;
}
HC_PARSE_WRAPPER(vtr_parse)
HC_DEFINE_READER(vtr_disk, XX_FILE_TYPE_VTR_DISK, "vtr")
