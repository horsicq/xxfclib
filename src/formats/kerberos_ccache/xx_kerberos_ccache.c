/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://web.mit.edu/kerberos/krb5-latest/doc/formats/ccache_file_format.html */
#include "xxfclib/formats/kerberos_ccache/xx_kerberos_ccache.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_network_packet.h"

static bool principal(Abstractformat *f, pm_stream *s, memory_blob *b, uint64_t *at, const char *label)
{
    uint64_t start = *at, p, n;
    if (!protocol_take(b, at, b->n, 8)) return false;
    uint32_t type = xx_data_get_u32(b->p + (size_t)start, 4, 0, true), count = xx_data_get_u32(b->p + (size_t)start + 4, 4, 0, true);
    if (!type || type > 10 || !count || count > 32 || !security_string(b, at, b->n, &p, &n, true) || !packet_utf(b, p, n)) return false;
    for (unsigned i = 0; i < count; ++i)
        if (!security_string(b, at, b->n, &p, &n, true) || !packet_utf(b, p, n)) return false;
    return blob_add(f, s, b, label, start, *at - start);
}
static bool wrapped(memory_blob *b, uint64_t *at, uint64_t end, uint8_t outer, uint8_t inner, der_tlv *t)
{
    der_tlv x;
    if (!der_take(b, at, end, outer, &x)) return false;
    uint64_t p = x.value;
    return der_take(b, &p, x.end, inner, t) && p == x.end;
}
static bool ticket(memory_blob *b, uint64_t p, uint64_t n)
{
    uint64_t at = p, end = p + n, q, x, v;
    der_tlv app, seq, t, u, fields;
    unsigned count = 0;
    if (n > 1048576 || !der_take(b, &at, end, 97, &app) || at != end) return false;
    q = app.value;
    if (!der_take(b, &q, app.end, 48, &seq) || q != app.end) return false;
    q = seq.value;
    if (!wrapped(b, &q, seq.end, 160, 2, &t) || !protocol_uint(b, &t, &v) || v != 5 || !wrapped(b, &q, seq.end, 161, 27, &t) ||
        !packet_utf(b, t.value, t.end - t.value) || !wrapped(b, &q, seq.end, 162, 48, &fields))
        return false;
    x = fields.value;
    if (!wrapped(b, &x, fields.end, 160, 2, &t) || !protocol_uint(b, &t, &v) || v > 10 || !wrapped(b, &x, fields.end, 161, 48, &u) || x != fields.end) return false;
    x = u.value;
    while (x < u.end) {
        if (++count > 32 || !der_take(b, &x, u.end, 27, &t) || !packet_utf(b, t.value, t.end - t.value)) return false;
    }
    if (!count || !wrapped(b, &q, seq.end, 163, 48, &fields) || q != seq.end) return false;
    x = fields.value;
    if (!wrapped(b, &x, fields.end, 160, 2, &t) || !protocol_uint(b, &t, &v) || !v || v > 65535) return false;
    if (x < fields.end && b->p[(size_t)x] == 161) {
        if (!wrapped(b, &x, fields.end, 161, 2, &t) || !protocol_uint(b, &t, &v) || v > UINT32_MAX) return false;
    }
    return wrapped(b, &x, fields.end, 162, 4, &t) && t.end > t.value && x == fields.end;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 4, end, p, n;
    unsigned count = 0, headers = 0;
    bool delta = false, ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 32 && b.p[0] == 5 && b.p[1] == 4);
    end = 4 + xx_data_get_u16(b.p + 2, 2, 0, true);
    BLOB_NEED(blob_span(&b, 4, end - 4));
    while (at < end) {
        BLOB_NEED(++headers <= 64 && protocol_take(&b, &at, end, 4));
        unsigned tag = xx_data_get_u16(b.p + (size_t)at - 4, 2, 0, true);
        n = xx_data_get_u16(b.p + (size_t)at - 2, 2, 0, true);
        BLOB_NEED(protocol_take(&b, &at, end, n));
        if (tag == 1) {
            BLOB_NEED(!delta && n == 8);
            delta = true;
        } else goto done;
    }
    BLOB_NEED(blob_add(f, s, &b, "cache-version-header", 0, end) && principal(f, s, &b, &at, "default-principal"));
    while (at < b.n) {
        BLOB_NEED(++count <= 512 && principal(f, s, &b, &at, "client-principal") && principal(f, s, &b, &at, "server-principal"));
        p = at;
        BLOB_NEED(protocol_take(&b, &at, b.n, 6));
        unsigned enctype = xx_data_get_u16(b.p + (size_t)p, 2, 0, true);
        n = xx_data_get_u32(b.p + (size_t)p + 2, 4, 0, true);
        BLOB_NEED(enctype > 0 && n > 0 && n <= 4096 && blob_add(f, s, &b, "key-metadata", p, 6) && protocol_take(&b, &at, b.n, n) &&
                  blob_add(f, s, &b, "session-key", at - n, n));
        p = at;
        BLOB_NEED(protocol_take(&b, &at, b.n, 21) && b.p[(size_t)p + 16] <= 1 && blob_add(f, s, &b, "credential-times-flags", p, 21));
        uint32_t auth = xx_data_get_u32(b.p + (size_t)p, 4, 0, true), start = xx_data_get_u32(b.p + (size_t)p + 4, 4, 0, true),
                 expiry = xx_data_get_u32(b.p + (size_t)p + 8, 4, 0, true), renew = xx_data_get_u32(b.p + (size_t)p + 12, 4, 0, true);
        BLOB_NEED(expiry >= auth && (!start || expiry >= start) && (!renew || renew >= expiry));
        for (unsigned group = 0; group < 2; ++group) {
            BLOB_NEED(protocol_take(&b, &at, b.n, 4));
            uint32_t items = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, true);
            BLOB_NEED(items <= 128);
            for (unsigned i = 0; i < items; ++i) {
                p = at;
                BLOB_NEED(protocol_take(&b, &at, b.n, 2));
                unsigned kind = xx_data_get_u16(b.p + (size_t)p, 2, 0, true);
                BLOB_NEED(kind > 0 && security_string(&b, &at, b.n, &p, &n, false) && n > 0);
                if (!group) BLOB_NEED((kind == 2 && n == 4) || (kind == 24 && n == 16));
                BLOB_NEED(blob_add(f, s, &b, group ? "encoded-authdata" : "address", p, n));
            }
        }
        BLOB_NEED(security_string(&b, &at, b.n, &p, &n, false) && n > 0 && ticket(&b, p, n) && blob_add(f, s, &b, "encoded-ticket", p, n));
        BLOB_NEED(security_string(&b, &at, b.n, &p, &n, false));
        if (n) BLOB_NEED(ticket(&b, p, n) && blob_add(f, s, &b, "encoded-second-ticket", p, n));
    }
    BLOB_NEED(count > 0);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_kerberos_ccache_init(xx_kerberos_ccache *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_KERBEROS_CCACHE, "bin");
    }
}
xx_kerberos_ccache *xx_kerberos_ccache_create(xx_io_device *d, int64_t b)
{
    xx_kerberos_ccache *r = (xx_kerberos_ccache *)xx_mem_alloc(sizeof(*r));
    if (r) xx_kerberos_ccache_init(r, d, b);
    return r;
}
void xx_kerberos_ccache_destroy(xx_kerberos_ccache *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_kerberos_ccache_free(xx_kerberos_ccache *r)
{
    if (r) {
        xx_kerberos_ccache_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_kerberos_ccache_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_kerberos_ccache_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
