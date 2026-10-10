/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/kode54/psflib/master/psflib.c
 * PSF1 nonempty program wrappers with bounded reserved/program/tag ranges, zlib framing and compressed-program CRC32. Exports reserved bytes, encoded program.zlib and
 * tag text; no inflation, library resolution, audio emulation or execution.
 */
#include "xxfclib/formats/sony_psf/xx_sony_psf.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool crc_range(Abstractformat *f, uint64_t at, uint64_t n, uint32_t expected, xx_pd_struct *pd)
{
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *b = NULL;
    bool buffer_result = false;
    uint32_t crc = 0U;
    while (n) {
        if (!b) {
            if ((uint64_t)(n) < capacity) capacity = (size_t)(n);
            b = (uint8_t *)xx_mem_alloc(capacity);
            if (!b) {
                buffer_result = false;
                goto buffer_done;
            }
        }
        size_t part = n > capacity ? capacity : (size_t)n;
        if (stop(pd) || !pm_read(f, (int64_t)at, b, part)) {
            buffer_result = (false);
            goto buffer_done;
        }
        crc = xx_crc32_calc(crc, b, part);
        at += part;
        n -= part;
    }
    {
        buffer_result = (crc == expected);
        goto buffer_done;
    }

buffer_done:
    xx_mem_free(b);
    return buffer_result;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[16], z[2], c;
    uint32_t reserved, n, crc;
    uint64_t at, total, left, i;
    bool equals = false, line = false;
    unsigned length = 0;
    if (!pm_read(f, 0, h, 16) || xx_rt_memcmp(h, "PSF\x01", 4)) {
        return false;
    }
    reserved = xx_data_get_u32(h + 4, 4, 0, false);
    n = xx_data_get_u32(h + 8, 4, 0, false);
    crc = xx_data_get_u32(h + 12, 4, 0, false);
    at = 16U + (uint64_t)reserved;
    total = at + n;
    left = (uint64_t)pm_available(f);
    if (reserved > 16777216 || n < 6 || n > 67108864 || total > left || !pm_read(f, (int64_t)at, z, 2) || (z[0] & 15) != 8 || (z[0] >> 4) > 7 ||
        (((unsigned)z[0] << 8) | z[1]) % 31 || (z[1] & 32) || !crc_range(f, at, n, crc, pd))
        return false;
    if (reserved && !emit(f, s, "reserved.bin", 16, reserved, left)) {
        return false;
    }
    if (!emit(f, s, "program.zlib", at, n, left)) return false;
    if (left > total) {
        uint8_t tag[5];
        if (left - total < 5 || left - total > 1048576 || !pm_read(f, (int64_t)total, tag, 5) || xx_rt_memcmp(tag, "[TAG]", 5)) return false;
        for (i = total + 5; i < left; ++i) {
            if (stop(pd) || !pm_read(f, (int64_t)i, &c, 1) || !c || (c < 32 && c != 10 && c != 13 && c != 9)) return false;
            if (c == 10) {
                if (line && !equals) return false;
                line = equals = false;
                length = 0;
            } else if (c != 13) {
                if (++length > 4096) return false;
                if (c == '=' && line) equals = true;
                line = true;
            }
        }
        if (line && !equals) {
            return false;
        }
        if (left > total + 5 && !emit(f, s, "tags.txt", total + 5, left - total - 5, left)) return false;
        total = left;
    }
    s->size = (int64_t)total;
    return true;
}

void xx_sony_psf_init(xx_sony_psf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SONY_PSF, "psf");
    }
}
xx_sony_psf *xx_sony_psf_create(xx_io_device *d, int64_t b)
{
    xx_sony_psf *r = (xx_sony_psf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sony_psf_init(r, d, b);
    return r;
}
void xx_sony_psf_destroy(xx_sony_psf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sony_psf_free(xx_sony_psf *r)
{
    if (r) {
        xx_sony_psf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sony_psf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sony_psf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
