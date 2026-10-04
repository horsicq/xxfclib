/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * SAD's 22-byte header and side-sequential order are documented by the
 * original HxC sad_fileformat.h. Exports cylinder/head-ordered raw sectors.
 */
#include "xxfclib/formats/samcoupe_sad/xx_samcoupe_sad.h"
#include "../xx_hxc_sector.h"
static bool sad_parse(Abstractformat *f, pm_stream *s, hc_blob *b) {
    xx_samcoupe_sad *r = (xx_samcoupe_sad *)f;
    uint32_t total, track, c, h; uint8_t *out;
    if (!hc_span_ok(b, 0U, 22U) || xx_rt_memcmp(b->p, "Aley's disk backup", 18U) ||
        !hc_geometry(r, b->p[19], b->p[18], b->p[20], (uint32_t)b->p[21] * 64U, &total) ||
        total != b->n - 22U) return false;
    out = hc_alloc(b, total); if (!out) return false;
    track = r->sectors_per_track * r->sector_size;
    for (c = 0U; c < r->cylinders; ++c) for (h = 0U; h < r->heads; ++h) {
        if (!hc_poll(b)) { xx_mem_free(out); return false; }
        xx_rt_memcpy(out + (c * r->heads + h) * track, b->p + 22U + (h * r->cylinders + c) * track, track);
    }
    r->note = "side-sequential sectors normalized to cylinder/head order";
    return hc_memory(f, s, b, "disk.img", out, total);
}
HC_PARSE_WRAPPER(sad_parse)
HC_DEFINE_READER(samcoupe_sad, XX_FILE_TYPE_SAMCOUPE_SAD, "sad")
