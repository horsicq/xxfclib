/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/renpy/renpy/master/renpy/loader.py
 * RPA3 with zlib index and restricted protocol2 pickle dictionary of one (offset,length) tuple per file. Decodes the index without executing pickle opcodes. Prefix
 * bytes, chunk lists, object construction/memo references and other pickle protocols rejected; index scratch capped at12MiB and any lower explicit budget.
 */
#include "xxfclib/formats/renpy_rpa/xx_renpy_rpa.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p, bool be)
{
    return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true) << 32) | xx_data_get_u32(p + 4, 4, 0, true)
              : ((uint64_t)xx_data_get_u32(p + 4, 4, 0, false) << 32) | xx_data_get_u32(p, 4, 0, false);
}
static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i) {
        uint64_t a = (uint64_t)(s->items[i].offset - f->base_address), b = (uint64_t)s->items[i].size;
        if (n && b && at < a + b && a < at + n) return false;
    }
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f, uint64_t at, uint64_t end)
{
    uint8_t c;
    uint64_t i;
    if (at >= end || end > (uint64_t)pm_available(f)) return false;
    for (i = 0; i < 4096 && at + i < end; ++i) {
        if (!pm_read(f, (int64_t)(at + i), &c, 1)) return false;
        if (!c) return i != 0;
    }
    return false;
}

#include "xxfclib/algo/deflate/xx_deflate.h"

typedef struct rp {
    const uint8_t *p;
    size_t size, at;
} rp;
static bool rb(rp *r, uint8_t b)
{
    return r->at < r->size && r->p[r->at++] == b;
}
static bool memo(rp *r)
{
    if (r->at >= r->size) return false;
    if (r->p[r->at] == 'q') {
        if (r->size - r->at < 2) return false;
        r->at += 2;
    } else if (r->p[r->at] == 'r') {
        if (r->size - r->at < 5) return false;
        r->at += 5;
    }
    return true;
}
static bool integer(rp *r, uint64_t *v)
{
    uint8_t op, n;
    size_t i;
    if (r->at >= r->size) return false;
    op = r->p[r->at++];
    if (op == 'K') {
        if (r->at >= r->size) return false;
        *v = r->p[r->at++];
        return true;
    }
    if (op == 'M') {
        if (r->size - r->at < 2) return false;
        *v = xx_data_get_u16(r->p + r->at, 2, 0, false);
        r->at += 2;
        return true;
    }
    if (op == 'J') {
        if (r->size - r->at < 4 || r->p[r->at + 3] & 128) return false;
        *v = xx_data_get_u32(r->p + r->at, 4, 0, false);
        r->at += 4;
        return true;
    }
    if (op != 0x8a || r->at >= r->size) {
        return false;
    }
    n = r->p[r->at++];
    if (!n || n > 8 || r->size - r->at < n || r->p[r->at + n - 1] & 128) return false;
    *v = 0;
    for (i = 0; i < n; ++i) *v |= (uint64_t)r->p[r->at + i] << (i * 8);
    r->at += n;
    return true;
}
static bool keyname(rp *r)
{
    uint8_t op;
    uint32_t n;
    if (r->at >= r->size) return false;
    op = r->p[r->at++];
    if (op == 'X' || op == 'T') {
        if (r->size - r->at < 4) return false;
        n = xx_data_get_u32(r->p + r->at, 4, 0, false);
        r->at += 4;
    } else if (op == 'U') {
        if (r->at >= r->size) return false;
        n = r->p[r->at++];
    } else return false;
    if (!n || n > 4096 || r->size - r->at < n) {
        return false;
    }
    r->at += n;
    return memo(r);
}
static bool hexnumber(const uint8_t *p, size_t n, uint64_t *value)
{
    size_t i;
    *value = 0;
    for (i = 0; i < n; ++i) {
        uint8_t c = p[i];
        unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (d > 15) return false;
        *value = (*value << 4) | d;
    }
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[34], *packed = NULL, *plain = NULL;
    uint64_t index, key, budget = 12U * 1024U * 1024U;
    size_t n, used = 0, written = 0;
    xx_io_device *out = NULL;
    bool ok = false, batch;
    rp r;
    unsigned i = 0;
    const xx_var *opt;
    if (!pm_read(f, 0, h, 34) || xx_rt_memcmp(h, "RPA-3.0 ", 8) || h[24] != ' ' || h[33] != '\n' || !hexnumber(h + 8, 16, &index) || !hexnumber(h + 25, 8, &key) ||
        index < 34 || index > (uint64_t)pm_available(f))
        return false;
    n = (size_t)((uint64_t)pm_available(f) - index);
    if (n > 4U * 1024U * 1024U) n = 4U * 1024U * 1024U;
    opt = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (opt && xx_var_get_u64(opt) < budget) budget = xx_var_get_u64(opt);
    if (n < 6 || n + 8U * 1024U * 1024U > budget) return false;
    packed = (uint8_t *)xx_mem_alloc(n);
    plain = (uint8_t *)xx_mem_alloc(8U * 1024U * 1024U);
    if (!packed || !plain || !pm_read(f, (int64_t)index, packed, n) || !xx_zlib_stream_header_is_valid(packed, n)) goto done;
    out = xx_io_mem_open(plain, 8U * 1024U * 1024U);
    if (!out) goto done;
    ok = xx_deflate_unpack_memory_to_device_ex(packed + 2, n - 2, out, &used, false, pd);
    written = (size_t)xx_io_tell(out);
    xx_io_close(out);
    out = NULL;
    if (!ok || used > n - 6 || xx_zlib_stream_adler32(plain, written) != xx_data_get_u32(packed + 2 + used, 4, 0, true)) {
        ok = false;
        goto done;
    }
    ok = false;
    r.p = plain;
    r.size = written;
    r.at = 0;
    if (!rb(&r, 0x80) || !rb(&r, 2) || !rb(&r, '}') || !memo(&r)) goto done;
    batch = r.at < r.size && r.p[r.at] == '(';
    if (batch) ++r.at;
    while (r.at < r.size && r.p[r.at] != (batch ? 'u' : '.')) {
        uint64_t at, size;
        char label[40];
        if (i >= 4096 || (pd && xx_pd_is_stopped(pd)) || !keyname(&r) || !rb(&r, ']') || !memo(&r) || !integer(&r, &at) || !integer(&r, &size) || !rb(&r, 0x86) ||
            !memo(&r) || !rb(&r, 'a') || (!batch && !rb(&r, 's')))
            goto done;
        at ^= key;
        size ^= key;
        if (at < 34 || !span(at, size, index)) goto done;
        xx_rt_snprintf(label, sizeof(label), "file-%u.bin", i++);
        if (!emit(f, s, label, at, size, index + used + 6)) goto done;
        if (!batch) break;
    }
    if ((batch && !rb(&r, 'u')) || !rb(&r, '.') || r.at != r.size || !i) goto done;
    s->size = (int64_t)(index + used + 6);
    ok = true;
done:
    if (out) xx_io_close(out);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return ok;
}

void xx_renpy_rpa_init(xx_renpy_rpa *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_RENPY_RPA, "rpa");
    }
}
xx_renpy_rpa *xx_renpy_rpa_create(xx_io_device *d, int64_t b)
{
    xx_renpy_rpa *r = (xx_renpy_rpa *)xx_mem_alloc(sizeof(*r));
    if (r) xx_renpy_rpa_init(r, d, b);
    return r;
}
void xx_renpy_rpa_destroy(xx_renpy_rpa *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_renpy_rpa_free(xx_renpy_rpa *r)
{
    if (r) {
        xx_renpy_rpa_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_renpy_rpa_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_renpy_rpa_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
