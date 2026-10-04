/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Explicit-only EII sample component:136 stored3584-byte head tracks map to
 * cylinder11/head0 through cylinder78/head1. OS tracks are not present.
 * Original HxC emuii_loader.c defines this mapping. No size-only auto probe.
 */
#include "xxfclib/formats/emulatorii_eii/xx_emulatorii_eii.h"
#include "../xx_hxc_sector.h"
static bool eii_parse(Abstractformat *f, pm_stream *s, hc_blob *b) {
    xx_emulatorii_eii *r = (xx_emulatorii_eii *)f; uint32_t i; char name[48];
    if (b->n != 136U * 3584U) return false;
    r->cylinders = 80U; r->heads = 2U; r->sectors_per_track = 1U; r->sector_size = 3584U;
    r->incomplete = true; r->note = "original sample tracks only; complete disk requires emuiios.emuiifd OS tracks";
    for (i = 0U; i < 136U; ++i) {
        xx_rt_snprintf(name, sizeof(name), "track-%03u-head-%u.bin", (i + 22U) / 2U, (i + 22U) % 2U);
        if (!hc_emit(f, s, b, name, i * 3584U, 3584U)) return false;
    }
    return true;
}
HC_PARSE_WRAPPER(eii_parse)
HC_DEFINE_READER(emulatorii_eii, XX_FILE_TYPE_EMULATORII_EII, "eii")
