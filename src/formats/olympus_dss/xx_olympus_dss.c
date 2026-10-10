/* SPDX-License-Identifier: MIT.
 * Olympus DSS header and 512-byte audio blocks:
 * https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/dss.c
 * DS2 and ENC recognition follows Detect It Easy; payload remains encoded.
 */
#include "xxfclib/formats/olympus_dss/xx_olympus_dss.h"
#include "../common/xx_binary_cursor.h"
#ifndef OLYMPUS_DSS
#define XX_FILE_TYPE_OLYMPUS_DSS ((xx_file_type_t)1511)
#endif
static bool digits(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] < '0' || p[i] > '9') return false;
    return true;
}
static bool timestamp(const uint8_t *p)
{
    unsigned mo, da, hh, mm, ss;
    if (!digits(p, 12)) return false;
    mo = (unsigned)(p[2] - '0') * 10U + p[3] - '0';
    da = (unsigned)(p[4] - '0') * 10U + p[5] - '0';
    hh = (unsigned)(p[6] - '0') * 10U + p[7] - '0';
    mm = (unsigned)(p[8] - '0') * 10U + p[9] - '0';
    ss = (unsigned)(p[10] - '0') * 10U + p[11] - '0';
    return mo >= 1 && mo <= 12 && da >= 1 && da <= 31 && hh <= 23 && mm <= 59 && ss <= 59;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[0x44];
    uint32_t header;
    int64_t n;
    bool dss;
    if (binary_stop(pd) || (n = pm_available(f)) < 0x401 || !pm_read(f, 0, h, sizeof(h)) || (h[0] != 2 && h[0] != 3)) return false;
    dss = !xx_rt_memcmp(h + 1, "dss", 3);
    if (!dss && xx_rt_memcmp(h + 1, "ds2", 3) && xx_rt_memcmp(h + 1, "enc", 3)) return false;
    header = (uint32_t)h[0] * 512U;
    if (header > (uint64_t)n || !timestamp(h + 0x26) || !timestamp(h + 0x32) || !digits(h + 0x3e, 6) || h[0x40] > '5' || h[0x42] > '5') return false;
    if (!pm_add(f, s, "dss-header.bin", 0, header) || !pm_add(f, s, "coded-audio.bin", header, n - header)) return false;
    s->size = n;
    return true;
}
void xx_olympus_dss_init(xx_olympus_dss *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_OLYMPUS_DSS, "dss");
    }
}
xx_olympus_dss *xx_olympus_dss_create(xx_io_device *d, int64_t b)
{
    xx_olympus_dss *r = (xx_olympus_dss *)xx_mem_alloc(sizeof(*r));
    if (r) xx_olympus_dss_init(r, d, b);
    return r;
}
void xx_olympus_dss_destroy(xx_olympus_dss *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_olympus_dss_free(xx_olympus_dss *r)
{
    if (r) {
        xx_olympus_dss_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_olympus_dss_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_olympus_dss_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
