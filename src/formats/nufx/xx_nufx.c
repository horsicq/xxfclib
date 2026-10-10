/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native NuFX: stored/Squeeze/LZW1/LZW2, version-specific CRCs.
 * Primary: https://nulib.com/library/FTN.e08002.htm
 * Existing synthetic fork output names are retained for compatibility.
 */
#include "xxfclib/formats/nufx/xx_nufx.h"
#include "../apple_family/xx_apple_family_private.h"
#include "xxfclib/data/xx_data.h"
#include "../apple_family/xx_apple_squeeze.h"
#include "xx_nulzw.h"
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    af_work w;
    af_blob b;
    uint32_t count, i, total = 0;
    uint64_t at = 48;
    bool ok = false;
    if (!af_init(&w, f, s, pd) || !af_load(&w, &b)) return false;
    if (b.n < 48U ||
        xx_rt_memcmp(b.p,
                     "N\xf5"
                     "F\xe9"
                     "l\xe5",
                     6) ||
        xx_data_get_u16(b.p + 28, 2, 0, false) > 2U || af_crc16(&w, b.p + 8, 40, 0) != xx_data_get_u16(b.p + 6, 2, 0, false) ||
        (count = xx_data_get_u32(b.p + 8, 4, 0, false)) > AF_COUNT_MAX || !af_zero(b.p + 30, 8) || !af_zero(b.p + 42, 6) ||
        (xx_data_get_u16(b.p + 28, 2, 0, false) == 2U && xx_data_get_u32(b.p + 38, 4, 0, false) != b.n))
        goto done;
    for (i = 0; i < count; ++i) {
        const uint8_t *h;
        uint16_t attrs, version, filename;
        uint32_t threads, j;
        uint64_t table, data, end;
        bool named = false;
        unsigned fork_seen = 0;
        if (!af_poll(&w) || !af_range(&b, at, 58U) ||
            xx_rt_memcmp(b.p + at,
                         "N\xf5"
                         "F\xd8",
                         4))
            goto done;
        h = b.p + at;
        attrs = xx_data_get_u16(h + 6, 2, 0, false);
        version = xx_data_get_u16(h + 8, 2, 0, false);
        threads = xx_data_get_u32(h + 10, 4, 0, false);
        if (version > 3U || attrs < (version ? 60U : 58U) || attrs > 4096U || !af_range(&b, at, attrs) || !threads || threads > AF_COUNT_MAX - total) goto done;
        if (version && (xx_data_get_u16(h + 56, 2, 0, false) > attrs - 60U || 58U + ((xx_data_get_u16(h + 56, 2, 0, false) + 1U) & ~1U) > (unsigned)attrs - 2U))
            goto done;
        filename = xx_data_get_u16(h + attrs - 2U, 2, 0, false);
        if (filename > 1024U || !af_range(&b, at + attrs, filename)) goto done;
        named = filename != 0;
        table = at + attrs + filename;
        if (!af_range(&b, table, (uint64_t)threads * 16U)) goto done;
        data = table + (uint64_t)threads * 16U;
        end = data;
        if (af_crc16(&w, h + 6, (size_t)(data - at - 6U), 0) != xx_data_get_u16(h + 4, 2, 0, false)) goto done;
        for (j = 0; j < threads; ++j) {
            const uint8_t *t = b.p + table + j * 16U;
            uint16_t cls = xx_data_get_u16(t, 2, 0, false), method = xx_data_get_u16(t + 2, 2, 0, false), kind = xx_data_get_u16(t + 4, 2, 0, false);
            uint64_t raw = xx_data_get_u32(t + 8, 4, 0, false);
            uint32_t packed = xx_data_get_u32(t + 12, 4, 0, false);
            uint8_t *out;
            char label[96];
            if (!af_poll(&w) || cls > 3U || !af_range(&b, end, packed)) goto done;
            if (cls == 2U) {
                if (kind > 2U || method > 3U || (fork_seen & (1U << kind))) goto done;
                fork_seen |= 1U << kind;
                if (kind == 1U && xx_data_get_u32(h + 26, 4, 0, false)) raw = (uint64_t)xx_data_get_u32(h + 26, 4, 0, false) * 512U;
                if (raw > AF_INPUT_MAX || raw > w.member_limit || ((fork_seen & 2U) && (fork_seen & 5U))) goto done;
                if (version == 2U && af_crc16(&w, b.p + end, packed, 65535U) != xx_data_get_u16(t + 6, 2, 0, false)) goto done;
                out = af_alloc(&w, raw, true);
                if (!out) goto done;
                if (method == 0U) {
                    if (raw != packed) {
                        af_release(&w, out, raw);
                        goto done;
                    }
                    if (raw) xx_rt_memcpy(out, b.p + end, (size_t)raw);
                } else if ((method == 1U && !as_decode(&w, b.p + end, packed, out, (uint32_t)raw)) ||
                           (method >= 2U && !nl_decode(&w, b.p + end, packed, out, (uint32_t)raw, method))) {
                    af_release(&w, out, raw);
                    goto done;
                }
                if (version == 3U && af_crc16(&w, out, (size_t)raw, 65535U) != xx_data_get_u16(t + 6, 2, 0, false)) {
                    af_release(&w, out, raw);
                    goto done;
                }
                xx_rt_snprintf(label, sizeof(label), "%04u-record-%u-%s.bin", (unsigned)s->count, i, kind == 0U ? "data" : kind == 1U ? "disk" : "resource");
                if (!af_add(&w, label, 0, raw, out)) {
                    af_release(&w, out, raw);
                    goto done;
                }
                s->items[s->count - 1U].packed_size = packed;
                s->items[s->count - 1U].compression_method = method;
            } else if (cls == 3U) {
                if (method || kind || !raw || raw > packed || packed > 4096U) goto done;
                named = true;
            } else if (cls == 1U) {
                if (method || kind || raw || packed) goto done;
            } else if (method || kind > 2U || raw > packed || packed > 65536U) goto done;
            end += packed;
        }
        if (!named) {
            goto done;
        }
        total += threads;
        at = end;
    }
    ok = at == b.n && af_poll(&w);
    if (ok) {
        s->size = b.n;
        ((xx_nufx *)f)->number_of_records = s->count;
        ((xx_nufx *)f)->note = "Native stored/Squeeze/LZW1/LZW2; header CRC and applicable thread/chunk CRC checked";
    }
done:
    af_release(&w, b.p, b.n);
    return ok;
}
AF_DEFINE_READER(nufx, XX_FILE_TYPE_NUFX, "shk")
