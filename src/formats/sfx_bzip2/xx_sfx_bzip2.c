/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_bzip2/xx_sfx_bzip2.h"
#include "../common/xx_executable_carrier.h"

static bool executable_carrier_bz_at(Abstractformat *f, pm_stream *s, int64_t at, xx_pd_struct *pd)
{
    int64_t available = pm_available(f) - at;
    uint8_t *input = NULL, *output = NULL;
    size_t n, i, written;
    unsigned tries = 0;
    bool ok = false;
    if (available < 14) {
        return false;
    }
    n = available > 16777216 ? 16777216U : (size_t)available;
    input = (uint8_t *)xx_mem_alloc(n);
    if (!input || !pm_read(f, at, input, n) || input[3] < '1' || input[3] > '9') goto done;
    for (i = 4; i + 10 <= n; ++i) {
        uint64_t v = 0;
        unsigned j, shift;
        if ((i & 4095U) == 0 && carrier_stop(pd)) goto done;
        for (j = 0; j < 7; ++j) v = (v << 8) | input[i + j];
        for (shift = 0; shift < 8; ++shift)
            if (((v >> (8 - shift)) & UINT64_C(0xffffffffffff)) == UINT64_C(0x177245385090)) {
                size_t bytes = i + (80 + shift + 7) / 8;
                unsigned unused = (8 - shift) & 7;
                if (bytes > n || (unused && (input[bytes - 1] & ((1U << unused) - 1)))) {
                    continue;
                }
                if (++tries > 16) goto done;
                if (!output) output = (uint8_t *)xx_mem_alloc(67108864);
                if (!output) goto done;
                {
                    xx_io_device *source = xx_io_mem_open_ro(input, bytes);
                    bool decoded = source && xx_bzip2_unpack_device_to_memory(source, 0, (int64_t)bytes, output, 67108864, &written, pd);
                    if (source) xx_io_close(source);
                    if (decoded && !carrier_stop(pd)) {
                        ok = executable_carrier_component(f, s, at, (int64_t)bytes, "payload.bz2");
                        goto done;
                    }
                }
            }
    }
done:
    if (input) xx_mem_free(input);
    if (output) xx_mem_free(output);
    return ok;
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {'B', 'Z', 'h'};
    return executable_carrier_scan(f, s, sig, 3, 0, true, true, executable_carrier_bz_at, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_sfx_bzip2_init(xx_sfx_bzip2 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_BZIP2, "exe");
    }
}
xx_sfx_bzip2 *xx_sfx_bzip2_create(xx_io_device *d, int64_t b)
{
    xx_sfx_bzip2 *r = (xx_sfx_bzip2 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_bzip2_init(r, d, b);
    return r;
}
void xx_sfx_bzip2_destroy(xx_sfx_bzip2 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_bzip2_free(xx_sfx_bzip2 *r)
{
    if (r) {
        xx_sfx_bzip2_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_bzip2_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_bzip2_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
