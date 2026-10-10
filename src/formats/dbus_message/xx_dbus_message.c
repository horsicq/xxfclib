/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://dbus.freedesktop.org/doc/dbus-specification.html */
#include "xxfclib/formats/dbus_message/xx_dbus_message.h"
#include "../common/xx_serialized_value_helpers.h"

static unsigned db_align(uint8_t c)
{
    return c == 'y' || c == 'g' || c == 'v' ? 1 : c == 'n' || c == 'q' ? 2 : c == 'b' || c == 'i' || c == 'u' || c == 's' || c == 'o' || c == 'a' ? 4 : 8;
}
static bool db_basic(uint8_t c)
{
    return c == 'y' || c == 'b' || c == 'n' || c == 'q' || c == 'i' || c == 'u' || c == 'x' || c == 't' || c == 'd' || c == 's' || c == 'o' || c == 'g';
}
static bool db_type(const uint8_t *sig, uint64_t n, uint64_t *at, unsigned depth, bool dict)
{
    uint8_t c;
    uint64_t start;
    if (depth > 32 || *at >= n) return false;
    c = sig[(size_t)(*at)++];
    if (db_basic(c) || c == 'v') return true;
    if (c == 'a') return db_type(sig, n, at, depth + 1, true);
    if (c == '(') {
        start = *at;
        while (*at < n && sig[(size_t)*at] != ')')
            if (!db_type(sig, n, at, depth + 1, false)) return false;
        if (*at == start || *at >= n) return false;
        ++*at;
        return true;
    }
    if (c == '{' && dict) {
        if (*at >= n || !db_basic(sig[(size_t)*at]) || !db_type(sig, n, at, depth + 1, false) || !db_type(sig, n, at, depth + 1, false) || *at >= n ||
            sig[(size_t)(*at)++] != '}')
            return false;
        return true;
    }
    return false;
}
static bool db_signature(const uint8_t *sig, uint64_t n, bool single)
{
    uint64_t at = 0;
    unsigned count = 0;
    while (at < n) {
        if (++count > 255 || !db_type(sig, n, &at, 0, false)) return false;
    }
    return !single || count == 1;
}
static bool db_pad(memory_blob *b, uint64_t *at, uint64_t end, unsigned align)
{
    uint64_t n = (align - (*at & (align - 1))) & (align - 1);
    if (!record_span(*at, n, end) || !blob_zero(b, *at, n)) return false;
    *at += n;
    return true;
}
static bool db_path(memory_blob *b, uint64_t at, uint64_t n)
{
    bool component = false;
    if (!n || b->p[(size_t)at] != '/') return false;
    if (n == 1) return true;
    for (uint64_t i = 1; i < n; ++i) {
        uint8_t c = b->p[(size_t)(at + i)];
        if (c == '/') {
            if (!component) return false;
            component = false;
        } else {
            if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '_') return false;
            component = true;
        }
    }
    return component;
}
static bool db_name(memory_blob *b, uint64_t at, uint64_t n, uint8_t code)
{
    bool first = true, unique = false;
    unsigned dots = 0;
    if (!n || n > 255) return false;
    if ((code == 6 || code == 7) && b->p[(size_t)at] == ':') {
        unique = true;
        ++at;
        --n;
        if (!n) return false;
    }
    for (uint64_t i = 0; i < n; ++i) {
        uint8_t c = b->p[(size_t)(at + i)];
        if (c == '.') {
            if (first || code == 3) return false;
            first = true;
            ++dots;
            continue;
        }
        if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '_' && !((code == 6 || code == 7) && c == '-')) return false;
        if (first && !unique && c >= '0' && c <= '9') return false;
        first = false;
    }
    return !first && (code == 3 || dots);
}
static bool db_value(memory_blob *b, uint64_t *at, uint64_t end, const uint8_t *sig, uint64_t start, uint64_t stop, bool be, unsigned depth, unsigned *work)
{
    uint8_t c;
    uint64_t n, p, q, value;
    if (depth > 32 || ++*work > 1048576 || start >= stop) return false;
    c = sig[(size_t)start];
    if (!db_pad(b, at, end, db_align(c))) return false;
    if (c == 's' || c == 'o' || c == 'g') {
        unsigned width = c == 'g' ? 1 : 4;
        if (!record_span(*at, width, end) || !blob_span(b, *at, width)) return false;
        n = width == 1 ? b->p[(size_t)*at] : xx_data_get_u32(b->p + (size_t)*at, 4, 0, be);
        *at += width;
        if (!record_span(*at, n + 1, end) || !serialized_utf(b, *at, n) || b->p[(size_t)(*at + n)] || (c == 'g' && !db_signature(b->p + (size_t)*at, n, false)) ||
            (c == 'o' && !db_path(b, *at, n)))
            return false;
        *at += n + 1;
        return true;
    }
    if (c == 'v') {
        if (!record_span(*at, 1, end)) return false;
        n = b->p[(size_t)(*at)++];
        value = *at;
        if (!record_span(*at, n + 1, end) || !serialized_utf(b, *at, n) || b->p[(size_t)(*at + n)] || !db_signature(b->p + (size_t)*at, n, true)) return false;
        *at += n + 1;
        return db_value(b, at, end, b->p + (size_t)value, 0, n, be, depth + 1, work);
    }
    if (c == 'a') {
        if (!record_span(*at, 4, end) || !blob_span(b, *at, 4) || start + 1 >= stop) return false;
        n = xx_data_get_u32(b->p + (size_t)*at, 4, 0, be);
        *at += 4;
        if (!db_pad(b, at, end, db_align(sig[(size_t)start + 1])) || !record_span(*at, n, end)) return false;
        value = *at + n;
        if (sig[(size_t)start + 1] == 'y') {
            *at = value;
            return blob_span(b, value - n, n);
        }
        while (*at < value) {
            p = *at;
            if (!db_value(b, at, value, sig, start + 1, stop, be, depth + 1, work) || *at <= p) return false;
        }
        return *at == value;
    }
    if (c == '(' || c == '{') {
        p = start + 1;
        while (p < stop - 1) {
            q = p;
            if (!db_type(sig, stop - 1, &q, depth + 1, false) || !db_value(b, at, end, sig, p, q, be, depth + 1, work)) return false;
            p = q;
        }
        return p == stop - 1;
    }
    n = c == 'y' ? 1 : c == 'n' || c == 'q' ? 2 : c == 'b' || c == 'i' || c == 'u' ? 4 : c == 'x' || c == 't' || c == 'd' ? 8 : 0;
    if (!n || !record_span(*at, n, end) || !blob_span(b, *at, n)) return false;
    if (c == 'b' && xx_data_get_u32(b->p + (size_t)*at, 4, 0, be) > 1) return false;
    *at += n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 16, header, end, start, sigAt = 0, sigN = 0, p, q, body;
    uint32_t fields = 0;
    unsigned work = 0;
    bool be, ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(blob_span(&b, 0, 16) && (b.p[0] == 'l' || b.p[0] == 'B') && b.p[1] >= 1 && b.p[1] <= 4 && !(b.p[2] & ~7U) && b.p[3] == 1);
    be = b.p[0] == 'B';
    body = xx_data_get_u32(b.p + 4, 4, 0, be);
    header = 16 + (uint64_t)xx_data_get_u32(b.p + 12, 4, 0, be);
    BLOB_NEED(header <= b.n && xx_data_get_u32(b.p + 8, 4, 0, be));
    while (at < header) {
        uint8_t code, type;
        BLOB_NEED(db_pad(&b, &at, header, 8) && blob_span(&b, at, 3) && record_span(at, 3, header));
        start = at;
        code = b.p[(size_t)at++];
        BLOB_NEED(code && b.p[(size_t)at] == 1 && b.p[(size_t)at + 2] == 0);
        type = b.p[(size_t)at + 1];
        at += 3;
        if (code <= 9) {
            BLOB_NEED(!(fields & (1U << code)));
            fields |= 1U << code;
            BLOB_NEED(type == (code == 1 ? 'o' : code == 5 || code == 9 ? 'u' : code == 8 ? 'g' : 's'));
        }
        p = at;
        BLOB_NEED(db_value(&b, &at, header, &type, 0, 1, be, 0, &work));
        if ((code >= 2 && code <= 4) || code == 6 || code == 7) {
            BLOB_NEED(db_pad(&b, &p, header, 4) && db_name(&b, p + 4, xx_data_get_u32(b.p + (size_t)p, 4, 0, be), code));
        }
        if (code == 5) {
            BLOB_NEED(db_pad(&b, &p, header, 4) && xx_data_get_u32(b.p + (size_t)p, 4, 0, be));
        }
        if (code == 8) {
            sigN = b.p[(size_t)p];
            sigAt = p + 1;
        }
        if (code == 9) {
            BLOB_NEED(db_pad(&b, &p, header, 4) && !xx_data_get_u32(b.p + (size_t)p, 4, 0, be));
        }
        BLOB_NEED(blob_add(f, s, &b, "header-field", start, at - start));
    }
    BLOB_NEED(at == header && db_pad(&b, &at, b.n, 8) && record_span(at, body, b.n) && at + body == b.n);
    if (b.p[1] == 1) BLOB_NEED((fields & 10U) == 10U);
    else if (b.p[1] == 2) BLOB_NEED(fields & 32U);
    else if (b.p[1] == 3) BLOB_NEED((fields & 48U) == 48U);
    else BLOB_NEED((fields & 14U) == 14U);
    BLOB_NEED(blob_add(f, s, &b, "message-header", 0, 16) && (!body || sigN));
    end = b.n;
    p = 0;
    while (p < sigN) {
        q = p;
        start = at;
        BLOB_NEED(db_type(b.p + (size_t)sigAt, sigN, &q, 0, false) && db_value(&b, &at, end, b.p + (size_t)sigAt, p, q, be, 0, &work) &&
                  blob_add(f, s, &b, "body-argument", start, at - start));
        p = q;
    }
    BLOB_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_dbus_message_init(xx_dbus_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DBUS_MESSAGE, "dbus");
    }
}
xx_dbus_message *xx_dbus_message_create(xx_io_device *d, int64_t b)
{
    xx_dbus_message *r = (xx_dbus_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dbus_message_init(r, d, b);
    return r;
}
void xx_dbus_message_destroy(xx_dbus_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dbus_message_free(xx_dbus_message *r)
{
    if (r) {
        xx_dbus_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dbus_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dbus_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
