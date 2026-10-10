/* SPDX-License-Identifier: MIT. Original parser from documented field facts; no upstream implementation copied. */
#include "xxfclib/formats/ciscopy/xx_ciscopy.h"
#include "../disk_additions/xx_disk_additions.h"

/* DC-File uncompressed track map. Omitted tracks explicitly decode to F6. */
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[162];
    da_run runs[160];
    uint32_t entries, step, spt, i, count = 0;
    uint64_t at;
    uint8_t type;
    if (!da_read(f, 0, h, 1, pd) || (type = h[0]) < 1U || type > 7U) {
        return false;
    }
    entries = type <= 4U ? 80U : 160U;
    step = type <= 2U ? 2U : 1U;
    spt = (type == 1U || type == 3U) ? 8U : (type <= 5U ? 9U : (type == 6U ? 15U : 18U));
    if (!da_read(f, 0, h, entries + 2U, pd) || h[entries + 1U]) {
        return false;
    }
    at = entries + 2U;
    for (i = 0; i < entries; ++i)
        if (h[i + 1U] != 0x4cU && h[i + 1U] != 0xfaU && h[i + 1U] != 0xfeU) return false;
    for (i = 0; i < entries; i += step) {
        da_run *r = runs + count++;
        r->at = at;
        r->bytes = spt * 512U;
        r->count = 1U;
        r->stride = 0;
        r->source = NULL;
        r->fill = h[i + 1U] == 0x4cU ? -1 : 0xf6;
        if (r->fill < 0) at += r->bytes;
        if (!da_poll(pd)) return false;
    }
    if (at != (uint64_t)pm_available(f) || !da_add(f, s, "descriptor.dcf", 0, entries + 2U) || !da_map_add(f, s, "sector-image.img", runs, count)) {
        return false;
    }
    s->size = (int64_t)at;
    return true;
}
DA_API(ciscopy, XX_FILE_TYPE_CISCOPY, "dcf")
