/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/megastep/makeself/blob/master/makeself-header.sh
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/makeself/xx_makeself.h"
#include "../common/xx_carrier_helpers.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"

/* Makeself 1.x used a space after #! and tail +$skip, whose line number is
 * one-based. It did not emit the later filesizes table. Keep this legacy
 * grammar local to Makeself, and authenticate its complete bzip2/TAR tail. */
static char *ms_shell(Abstractformat *f, size_t *length)
{
    char *text = carrier_shell(f, length);
    int64_t available;
    size_t n;
    if (text) return text;
    available = pm_available(f);
    if (available < 11) return NULL;
    n = available > 65536 ? 65536U : (size_t)available;
    text = (char *)xx_mem_alloc(n + 1U);
    if (!text || !pm_read(f, 0, text, n) || xx_rt_memcmp(text, "#! /bin/sh\n", 11)) {
        xx_mem_free(text);
        return NULL;
    }
    text[n] = '\0';
    *length = n;
    return text;
}

static bool ms_legacy_bzip2(Abstractformat *f, pm_stream *s, int64_t offset, xx_pd_struct *pd)
{
    int64_t available = pm_available(f) - offset, written;
    uint8_t *packed = NULL, *plain = NULL;
    xx_io_device *sink = NULL, *view = NULL;
    Abstractformat tar;
    size_t consumed = 0U;
    bool okay = false;
    if (available < 14 || available > 33554432 || carrier_stop(pd)) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)available);
    plain = (uint8_t *)xx_mem_alloc(67108864U);
    if (!packed || !plain || !pm_read(f, offset, packed, (size_t)available) || xx_rt_memcmp(packed, "BZh", 3) || packed[3] < '1' || packed[3] > '9') goto done;
    sink = xx_io_mem_open(plain, 67108864U);
    if (!sink || !xx_bzip2_unpack_memory_to_device_ex(packed, (size_t)available, sink, &consumed, pd) || consumed != (size_t)available ||
        (written = xx_io_tell(sink)) <= 0 || carrier_stop(pd))
        goto done;
    view = xx_io_mem_open_ro(plain, (size_t)written);
    if (!view) goto done;
    xx_format_init(&tar, view, 0);
    okay = carrier_tar(&tar, 0, written, pd);
    xx_format_cleanup_extra_parameters(&tar);
    if (!okay || !pm_add(f, s, "payload-0.tar.bz2", offset, available)) {
        okay = false;
        goto done;
    }
    s->size = offset + available;
done:
    if (view) xx_io_close(view);
    if (sink) xx_io_close(sink);
    xx_mem_free(plain);
    xx_mem_free(packed);
    return okay;
}

static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    size_t len, n, i = 0;
    char *text = ms_shell(f, &len);
    const char *sizes, *p;
    uint64_t lines = 0;
    int64_t offset, limit = pm_available(f);
    unsigned count = 0;
    bool ok = false;
    if (!text) return false;
    if (!xx_rt_strstr(text, "This script was generated using Makeself")) goto done;
    if (!carrier_assignment(text, len, "filesizes", &sizes, &n) || !n) {
        const char *terminator = xx_rt_strstr(text, "\nEND_OF_STUB\n");
        if (!xx_rt_strstr(text, "This script was generated using Makeself 1.") || !terminator || !xx_rt_strstr(text, "tail +$skip $0") ||
            !xx_rt_strstr(text, "| bzip2 -d | tar") || !carrier_number(text, (size_t)(terminator - text), "skip", &lines) || lines < 2 ||
            !carrier_lines(f, lines - 1U, &offset, pd) || offset != (int64_t)(terminator - text) + 13)
            goto done;
        ok = ms_legacy_bzip2(f, s, offset, pd);
        goto done;
    }
    if (!carrier_number(text, len, "skip", &lines)) {
        p = xx_rt_strstr(text, "head -n ");
        if (!p) goto done;
        p += 8;
        {
            size_t k = 0;
            while (p[k] >= '0' && p[k] <= '9') ++k;
            if (!carrier_decimal(p, k, &lines)) goto done;
        }
    }
    if (!carrier_lines(f, lines, &offset, pd)) goto done;
    while (i < n) {
        uint64_t bytes;
        size_t begin;
        uint8_t h[6];
        const char *ext = "tar";
        char label[48];
        while (i < n && sizes[i] == ' ') {
            ++i;
        }
        begin = i;
        while (i < n && sizes[i] >= '0' && sizes[i] <= '9') ++i;
        if (begin == i || !carrier_decimal(sizes + begin, i - begin, &bytes) || !bytes || bytes < 6 || (i < n && sizes[i] != ' ') || ++count > 64 ||
            !carrier_range(limit, offset, bytes) || !pm_read(f, offset, h, 6) || carrier_stop(pd))
            goto done;
        if (h[0] == 31 && h[1] == 139 && h[2] == 8 && !(h[3] & 224)) ext = "tar.gz";
        else if (!xx_rt_memcmp(h, "BZh", 3) && h[3] >= '1' && h[3] <= '9') ext = "tar.bz2";
        else if (!xx_rt_memcmp(h,
                               "\xfd"
                               "7zXZ\0",
                               6))
            ext = "tar.xz";
        else if (h[0] == 31 && h[1] == 157 && !(h[2] & 96) && (h[2] & 31) >= 9 && (h[2] & 31) <= 16) ext = "tar.Z";
        else if (!carrier_tar(f, offset, offset + (int64_t)bytes, pd)) goto done;
        xx_rt_snprintf(label, sizeof(label), "payload-%u.%s", count - 1, ext);
        if (!pm_add(f, s, label, offset, (int64_t)bytes)) goto done;
        offset += (int64_t)bytes;
    }
    if (!count) {
        goto done;
    }
    s->size = offset;
    ok = true;
done:
    xx_mem_free(text);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_makeself_init(xx_makeself *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_MAKESELF, "run");
    }
}
xx_makeself *xx_makeself_create(xx_io_device *d, int64_t b)
{
    xx_makeself *r = (xx_makeself *)xx_mem_alloc(sizeof(*r));
    if (r) xx_makeself_init(r, d, b);
    return r;
}
void xx_makeself_destroy(xx_makeself *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_makeself_free(xx_makeself *r)
{
    if (r) {
        xx_makeself_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_makeself_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_makeself_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
