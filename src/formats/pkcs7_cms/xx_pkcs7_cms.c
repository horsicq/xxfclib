/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://www.rfc-editor.org/rfc/rfc5652.html */
#include "xxfclib/formats/pkcs7_cms/xx_pkcs7_cms.h"
#include "../common/xx_serialized_value_helpers.h"

typedef struct cm_tlv {
    uint8_t tag;
    uint64_t start, value, end;
} cm_tlv;
static bool cm_read(memory_blob *b, uint64_t *at, uint64_t end, cm_tlv *t)
{
    uint64_t n;
    uint8_t c;
    unsigned w;
    if (!record_span(*at, 2, end) || !blob_span(b, *at, 2)) return false;
    t->start = *at;
    t->tag = b->p[(size_t)(*at)++];
    if ((t->tag & 31) == 31 || !t->tag) return false;
    c = b->p[(size_t)(*at)++];
    n = c;
    if (c & 128) {
        w = c & 127;
        if (!w || w > 4 || !record_span(*at, w, end) || !blob_span(b, *at, w) || !b->p[(size_t)*at]) return false;
        n = 0;
        for (unsigned i = 0; i < w; ++i) n = (n << 8) | b->p[(size_t)(*at)++];
        if (n < 128) return false;
    }
    t->value = *at;
    if (!record_span(*at, n, end) || !blob_span(b, *at, n)) return false;
    t->end = *at + n;
    *at = t->end;
    return true;
}
static bool cm_take(memory_blob *b, uint64_t *at, uint64_t end, uint8_t tag, cm_tlv *t)
{
    return cm_read(b, at, end, t) && t->tag == tag;
}
static bool cm_oid(memory_blob *b, cm_tlv *t)
{
    uint64_t i = t->value;
    unsigned width = 0;
    if (t->tag != 6 || i == t->end) return false;
    while (i < t->end) {
        uint8_t c = b->p[(size_t)i++];
        if (!width && c == 128) return false;
        if (++width > 10) return false;
        if (!(c & 128)) width = 0;
    }
    return !width;
}
static bool cm_date(const uint8_t *v, uint64_t n, uint8_t tag)
{
    unsigned base = tag == 23 ? 2 : 4, year = 0, values[5], month, days;
    if (n != (tag == 23 ? 13U : 15U) || v[n - 1] != 'Z') return false;
    for (uint64_t i = 0; i < n - 1; ++i)
        if (v[i] < '0' || v[i] > '9') return false;
    for (unsigned i = 0; i < base; ++i) year = year * 10 + (unsigned)(v[i] - '0');
    if (tag == 23) year += year >= 50 ? 1900U : 2000U;
    for (unsigned i = 0; i < 5; ++i) values[i] = (unsigned)(v[base + i * 2] - '0') * 10 + (unsigned)(v[base + i * 2 + 1] - '0');
    month = values[0];
    if (!month || month > 12 || !values[1] || values[2] > 23 || values[3] > 59 || values[4] > 59) return false;
    days = month == 2 ? 28U + (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) : month == 4 || month == 6 || month == 9 || month == 11 ? 30U : 31U;
    return values[1] <= days;
}
static bool cm_tree(memory_blob *b, uint64_t at, uint64_t end, unsigned depth, unsigned *work)
{
    cm_tlv t;
    uint64_t prev = 0, prevn = 0;
    if (depth > 32) return false;
    while (at < end) {
        if (++*work > 65536 || !cm_read(b, &at, end, &t)) return false;
        uint64_t n = t.end - t.value;
        uint8_t *v = b->p + (size_t)t.value;
        if (t.tag & 32) {
            if (!cm_tree(b, t.value, t.end, depth + 1, work)) return false;
            if (t.tag == 49) {
                uint64_t pos = t.value;
                cm_tlv a;
                while (pos < t.end) {
                    if (!cm_read(b, &pos, t.end, &a) || (prevn && serialized_cmp(b->p + (size_t)prev, prevn, b->p + (size_t)a.start, a.end - a.start) > 0)) return false;
                    prev = a.start;
                    prevn = a.end - a.start;
                }
                prevn = 0;
            }
        } else if (t.tag == 1) {
            if (n != 1 || (*v != 0 && *v != 255)) return false;
        } else if (t.tag == 2) {
            if (!n || (n > 1 && ((v[0] == 0 && !(v[1] & 128)) || (v[0] == 255 && (v[1] & 128))))) return false;
        } else if (t.tag == 3) {
            if (!n || v[0] > 7 || (n == 1 && v[0]) || (n > 1 && (v[n - 1] & ((1U << v[0]) - 1)))) return false;
        } else if (t.tag == 5) {
            if (n) return false;
        } else if (t.tag == 6) {
            if (!cm_oid(b, &t)) return false;
        } else if (t.tag == 12) {
            if (!serialized_utf(b, t.value, n)) return false;
        } else if (t.tag == 23 || t.tag == 24) {
            if (!cm_date(v, n, t.tag)) return false;
        } else if (t.tag == 19 || t.tag == 22) {
            if (!blob_ascii(v, (size_t)n, false)) return false;
        } else if (t.tag == 30) {
            if (n & 1) return false;
        } else if (t.tag != 4 && t.tag != 20 && (t.tag & 192) != 128) return false;
    }
    return at == end;
}
static bool cm_alg(memory_blob *b, cm_tlv *t)
{
    uint64_t at = t->value;
    cm_tlv a;
    if (t->tag != 48 || !cm_take(b, &at, t->end, 6, &a) || !cm_oid(b, &a)) return false;
    if (at < t->end && !cm_read(b, &at, t->end, &a)) return false;
    return at == t->end;
}
static bool cm_cert(memory_blob *b, cm_tlv *t)
{
    uint64_t at = t->value, p;
    cm_tlv x, y, z;
    if (t->tag != 48 || !cm_take(b, &at, t->end, 48, &x) || !cm_take(b, &at, t->end, 48, &y) || !cm_alg(b, &y) || !cm_take(b, &at, t->end, 3, &z) || at != t->end)
        return false;
    p = x.value;
    if (p < x.end && b->p[(size_t)p] == 160) {
        cm_tlv ver, integer;
        if (!cm_take(b, &p, x.end, 160, &ver)) return false;
        uint64_t q = ver.value;
        if (!cm_take(b, &q, ver.end, 2, &integer) || q != ver.end || integer.end - integer.value != 1 || b->p[(size_t)integer.value] > 2) return false;
    }
    if (!cm_take(b, &p, x.end, 2, &z) || z.end == z.value || z.end - z.value > 21 || (b->p[(size_t)z.value] & 128) || !cm_take(b, &p, x.end, 48, &z) || !cm_alg(b, &z) ||
        !cm_take(b, &p, x.end, 48, &z) || !cm_take(b, &p, x.end, 48, &z))
        return false;
    uint64_t q = z.value;
    cm_tlv date;
    if (!cm_read(b, &q, z.end, &date) || (date.tag != 23 && date.tag != 24) || !cm_read(b, &q, z.end, &date) || (date.tag != 23 && date.tag != 24) || q != z.end ||
        !cm_take(b, &p, x.end, 48, &z) || !cm_take(b, &p, x.end, 48, &z))
        return false;
    q = z.value;
    if (!cm_take(b, &q, z.end, 48, &y) || !cm_alg(b, &y) || !cm_take(b, &q, z.end, 3, &y) || q != z.end) return false;
    uint8_t last = 128;
    while (p < x.end) {
        if (!cm_read(b, &p, x.end, &z) || z.tag <= last || (z.tag != 129 && z.tag != 130 && z.tag != 163)) return false;
        last = z.tag;
    }
    return p == x.end;
}
static bool cm_attr(memory_blob *b, cm_tlv *t)
{
    uint64_t at = t->value;
    cm_tlv a, x, y;
    unsigned count = 0;
    while (at < t->end) {
        if (++count > 1024 || !cm_take(b, &at, t->end, 48, &a)) return false;
        uint64_t p = a.value;
        if (!cm_take(b, &p, a.end, 6, &x) || !cm_oid(b, &x) || !cm_take(b, &p, a.end, 49, &y) || y.value == y.end || p != a.end) return false;
    }
    return count > 0;
}
static bool cm_signer(memory_blob *b, cm_tlv *t, bool *signer3)
{
    uint64_t at = t->value, p;
    cm_tlv x, y;
    uint8_t ver;
    if (t->tag != 48 || !cm_take(b, &at, t->end, 2, &x) || x.end - x.value != 1) return false;
    ver = b->p[(size_t)x.value];
    if (ver != 1 && ver != 3) return false;
    if (ver == 3) *signer3 = true;
    if (!cm_read(b, &at, t->end, &x)) return false;
    if (ver == 1) {
        if (x.tag != 48) return false;
        p = x.value;
        if (!cm_take(b, &p, x.end, 48, &y) || !cm_take(b, &p, x.end, 2, &y) || p != x.end) return false;
    } else if (x.tag != 128 || x.value == x.end) return false;
    if (!cm_take(b, &at, t->end, 48, &x) || !cm_alg(b, &x)) return false;
    if (at < t->end && b->p[(size_t)at] == 160) {
        if (!cm_take(b, &at, t->end, 160, &x) || !cm_attr(b, &x)) return false;
    }
    if (!cm_take(b, &at, t->end, 48, &x) || !cm_alg(b, &x) || !cm_take(b, &at, t->end, 4, &x) || x.value == x.end) return false;
    if (at < t->end && (!cm_take(b, &at, t->end, 161, &x) || !cm_attr(b, &x))) return false;
    return at == t->end;
}
static bool cm_data_oid(memory_blob *b, cm_tlv *t, uint8_t kind)
{
    static const uint8_t oid[] = {42, 134, 72, 134, 247, 13, 1, 7};
    return t->tag == 6 && t->end - t->value == 9 && !xx_rt_memcmp(b->p + (size_t)t->value, oid, 8) && b->p[(size_t)t->value + 8] == kind;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    cm_tlv root, oid, wrap, data, x, y, z;
    uint64_t at = 0, p, q;
    unsigned work = 0, certs = 0, signers = 0;
    uint8_t version = 0;
    bool signer3 = false, ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(cm_tree(&b, 0, b.n, 0, &work) && cm_take(&b, &at, b.n, 48, &root) && at == b.n);
    p = root.value;
    BLOB_NEED(cm_take(&b, &p, root.end, 6, &oid) && (cm_data_oid(&b, &oid, 1) || cm_data_oid(&b, &oid, 2)) && cm_take(&b, &p, root.end, 160, &wrap) && p == root.end &&
              blob_add(f, s, &b, "content-info", 0, wrap.value));
    q = wrap.value;
    if (cm_data_oid(&b, &oid, 1)) {
        BLOB_NEED(cm_take(&b, &q, wrap.end, 4, &data) && q == wrap.end && blob_add(f, s, &b, "content", data.value, data.end - data.value));
    } else {
        BLOB_NEED(cm_take(&b, &q, wrap.end, 48, &data) && q == wrap.end);
        p = data.value;
        BLOB_NEED(cm_take(&b, &p, data.end, 2, &x) && x.end - x.value == 1 && (b.p[(size_t)x.value] == 1 || b.p[(size_t)x.value] == 3));
        version = b.p[(size_t)x.value];
        BLOB_NEED(cm_take(&b, &p, data.end, 49, &x));
        q = x.value;
        while (q < x.end) BLOB_NEED(cm_take(&b, &q, x.end, 48, &y) && cm_alg(&b, &y));
        BLOB_NEED(blob_add(f, s, &b, "digest-algorithms", x.start, x.end - x.start) && cm_take(&b, &p, data.end, 48, &x));
        q = x.value;
        BLOB_NEED(cm_take(&b, &q, x.end, 6, &y) && cm_data_oid(&b, &y, 1));
        if (q < x.end) {
            BLOB_NEED(cm_take(&b, &q, x.end, 160, &y));
            uint64_t pos = y.value;
            BLOB_NEED(cm_take(&b, &pos, y.end, 4, &z) && pos == y.end && blob_add(f, s, &b, "content", z.value, z.end - z.value));
        }
        BLOB_NEED(q == x.end);
        if (p < data.end && b.p[(size_t)p] == 160) {
            BLOB_NEED(cm_take(&b, &p, data.end, 160, &x));
            q = x.value;
            while (q < x.end) {
                BLOB_NEED(++certs <= 1024 && cm_take(&b, &q, x.end, 48, &y) && cm_cert(&b, &y) && blob_add(f, s, &b, "certificate", y.start, y.end - y.start));
            }
        }
        BLOB_NEED(cm_take(&b, &p, data.end, 49, &x) && p == data.end);
        q = x.value;
        while (q < x.end)
            BLOB_NEED(++signers <= 1024 && cm_take(&b, &q, x.end, 48, &y) && cm_signer(&b, &y, &signer3) && blob_add(f, s, &b, "signer-info", y.start, y.end - y.start));
        BLOB_NEED((signers || certs) && version == (signer3 ? 3U : 1U));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_pkcs7_cms_init(xx_pkcs7_cms *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_PKCS7_CMS, "p7b");
    }
}
xx_pkcs7_cms *xx_pkcs7_cms_create(xx_io_device *d, int64_t b)
{
    xx_pkcs7_cms *r = (xx_pkcs7_cms *)xx_mem_alloc(sizeof(*r));
    if (r) xx_pkcs7_cms_init(r, d, b);
    return r;
}
void xx_pkcs7_cms_destroy(xx_pkcs7_cms *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_pkcs7_cms_free(xx_pkcs7_cms *r)
{
    if (r) {
        xx_pkcs7_cms_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_pkcs7_cms_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_pkcs7_cms_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
