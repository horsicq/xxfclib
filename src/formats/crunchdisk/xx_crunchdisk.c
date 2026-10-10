/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CrunchDisk CDF0/CYL0/CYL1 grammar and sector-byte-plane transpose.
 * Reference: libxad CrunchDisk.c. Original parser, existing MIT PP20 decoder. */
#include "xxfclib/formats/crunchdisk/xx_crunchdisk.h"
#include "../xx_legacy_archive.h"
static bool crunchdisk_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t bytes, sectors, heads, low, high, per, total, at = 32, c, i, j, plane;
    uint16_t method, eff;
    uint8_t *image;
    if (b->n < 32U || xx_rt_memcmp(b->p, "CDF0", 4)) return false;
    bytes = xx_data_get_u32(b->p + 4, 4, 0, true);
    sectors = xx_data_get_u32(b->p + 8, 4, 0, true);
    heads = xx_data_get_u32(b->p + 12, 4, 0, true);
    low = xx_data_get_u32(b->p + 16, 4, 0, true);
    high = xx_data_get_u32(b->p + 20, 4, 0, true);
    eff = xx_data_get_u16(b->p + 28, 2, 0, true);
    method = xx_data_get_u16(b->p + 30, 2, 0, true);
    if (bytes < 128U || bytes > 8192U || (bytes & (bytes - 1U)) || !sectors || sectors > 64U || !heads || heads > 2U || low > high || high > 255U || method > 2U ||
        eff > 4U || b->p[25])
        return false;
    if (b->p[24]) return ac_error(b, "password-protected CrunchDisk requires unsupported PX20 decryption");
    per = bytes * sectors * heads;
    if ((uint64_t)per * (high - low + 1U) > AC_MAX_BYTES) {
        return false;
    }
    total = per * (high - low + 1U);
    image = ac_alloc(b, total);
    if (!image) return false;
    for (c = low; c <= high; ++c) {
        uint32_t size, stored;
        uint8_t *plain = NULL;
        const uint8_t *source;
        if (!ac_poll(b) || !ac_span(b, at, 8U) || (xx_rt_memcmp(b->p + at, "CYL0", 4) && xx_rt_memcmp(b->p + at, "CYL1", 4))) goto fail;
        stored = size = xx_data_get_u32(b->p + at + 4, 4, 0, true);
        if (!ac_span(b, at + 8U, size)) goto fail;
        source = b->p + at + 8U;
        if (b->p[at + 3] == '0') {
            if (size != per) goto fail;
            xx_rt_memcpy(image + (c - low) * per, source, per);
        } else {
            if (method == 2U) {
                ac_error(b, "CrunchDisk XPK compression is unsupported");
                goto fail;
            }
            if (method == 1U) {
                uint8_t widths[4] = {9, 9, 9, 9};
                if (eff >= 1U) {
                    ++widths[1];
                    ++widths[2];
                    ++widths[3];
                }
                if (eff >= 2U) {
                    ++widths[2];
                    ++widths[3];
                }
                if (eff >= 3U) {
                    ++widths[2];
                    ++widths[3];
                }
                if (eff >= 4U) ++widths[3];
                plain = ac_alloc(b, per);
                if (!plain || !ac_pp(b, source, size, plain, per, widths)) {
                    if (plain) ac_release(b, plain, per);
                    goto fail;
                }
                source = plain;
                size = per;
            } else if (size > per || size % bytes) goto fail;
            for (i = 0; i < sectors * heads; ++i) {
                uint8_t *dest = image + (c - low) * per + i * bytes;
                if (i * bytes < size)
                    for (plane = 0; plane < 4U; ++plane)
                        for (j = 0; j < bytes / 4U; ++j) dest[j * 4U + plane] = source[i * bytes + plane * (bytes / 4U) + j];
                else xx_mem_zero(dest, bytes); /* producer explicitly omits whole zero sectors */
            }
            if (plain) ac_release(b, plain, per);
        }
        at += 8U + stored;
    }
    if (at != b->n) goto fail;
    ((xx_crunchdisk *)f)->note = "declared cylinder range decoded; explicitly omitted zero sectors expanded; no filesystem parsing";
    return ac_memory(f, s, b, "disk.img", image, total, b->n - 32U, method);
fail:
    ac_release(b, image, total);
    return false;
}
AC_PARSE(crunchdisk_parse)
AC_DEFINE(crunchdisk, XX_FILE_TYPE_CRUNCHDISK, "cdf")
