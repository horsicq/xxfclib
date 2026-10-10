/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * emaxutil1.1 EM1/EM2 wrapper: 39-byte literal producer header,56 bank blocks
 * and1024 sample blocks of512 bytes. These occupy disk blocks368..423 and
 * 440..1463. The520 OS blocks are absent; no disk bytes are synthesized.
 * Facts: original HxC emax_loader.c's format constants and stored extents.
 */
#include "xxfclib/formats/emax_disk/xx_emax_disk.h"
#include "../xx_hxc_sector.h"
static bool emax_parse(Abstractformat *f, pm_stream *s, hc_blob *b)
{
    static const char header[] = "emaxutil v1.1 Fri Mar 19 13:31:05 1993\n";
    xx_emax_disk *r = (xx_emax_disk *)f;
    if (sizeof(header) - 1U != 39U || b->n != 39U + 1080U * 512U || xx_rt_memcmp(b->p, header, 39U)) return false;
    r->cylinders = 80U;
    r->heads = 2U;
    r->sectors_per_track = 10U;
    r->sector_size = 512U;
    r->incomplete = true;
    r->note = "stored bank/sample blocks only; complete disk requires emaxos.emx (520 OS blocks)";
    return hc_emit(f, s, b, "bank-blocks-368-423.bin", 39U, 56U * 512U) && hc_emit(f, s, b, "sample-blocks-440-1463.bin", 39U + 56U * 512U, 1024U * 512U);
}
HC_PARSE_WRAPPER(emax_parse)
HC_DEFINE_READER(emax_disk, XX_FILE_TYPE_EMAX_DISK, "em1")
