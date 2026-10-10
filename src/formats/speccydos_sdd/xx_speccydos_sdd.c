/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * SpeccyDOS SDD layout facts: HxC sddfileformat.h. The boot sector belongs
 * to the raw data; sides are stored consecutively, sector size is 256.
 */
#include "xxfclib/formats/speccydos_sdd/xx_speccydos_sdd.h"
#include "../xx_hxc_sector.h"
static bool sdd_parse(Abstractformat *f, pm_stream *s, hc_blob *b)
{
    xx_speccydos_sdd *r = (xx_speccydos_sdd *)f;
    uint32_t c, h, total, per_track;
    uint8_t *out;
    if (!hc_span_ok(b, 0U, 256U) || xx_rt_memcmp(b->p, "TRKY2", 5U) || b->p[13] > 1U ||
        !hc_geometry(r, b->p[14] & 127U, (b->p[14] & 128U) ? 2U : 1U, b->p[15], 256U, &total) || total != b->n)
        return false;
    per_track = r->sectors_per_track * 256U;
    out = hc_alloc(b, total);
    if (!out) return false;
    for (c = 0U; c < r->cylinders; ++c)
        for (h = 0U; h < r->heads; ++h) {
            if (!hc_poll(b)) {
                xx_mem_free(out);
                return false;
            }
            xx_rt_memcpy(out + (c * r->heads + h) * per_track, b->p + (h * r->cylinders + c) * per_track, per_track);
        }
    r->note = b->p[13] ? "FM raw sectors; cylinder/head order" : "MFM raw sectors; cylinder/head order";
    return hc_memory(f, s, b, "disk.img", out, total);
}
HC_PARSE_WRAPPER(sdd_parse)
HC_DEFINE_READER(speccydos_sdd, XX_FILE_TYPE_SPECCYDOS_SDD, "sdd")
