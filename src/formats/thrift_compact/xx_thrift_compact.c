/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md */
#include "xxfclib/formats/thrift_compact/xx_thrift_compact.h"
#include "../common/xx_container_wire_helpers.h"

static bool tc_value(memory_blob *, uint64_t *, uint64_t, uint8_t, unsigned, unsigned *, bool);
static bool tc_struct(memory_blob *b, uint64_t *at, uint64_t end, unsigned depth, unsigned *work)
{
    int64_t id = 0, v;
    int64_t seen[256];
    unsigned fields = 0;
    while (*at < end) {
        uint8_t h = b->p[(size_t)(*at)++], kind = h & 15;
        if (!h) return true;
        if (!kind || kind > 13 || fields >= 256) return false;
        if (h >> 4) id += (h >> 4);
        else {
            if (!serialized_zig(b, at, end, &v)) return false;
            id = v;
        }
        if (id < -32768 || id > 32767) return false;
        for (unsigned i = 0; i < fields; ++i)
            if (seen[i] == id) return false;
        seen[fields] = id;
        ++fields;
        if (!tc_value(b, at, end, kind, depth + 1, work, true)) return false;
    }
    return false;
}
static bool tc_value(memory_blob *b, uint64_t *at, uint64_t end, uint8_t t, unsigned depth, unsigned *work, bool field)
{
    uint64_t n = 0, v;
    int64_t z;
    uint8_t kind, a, c;
    if (depth > 32 || ++*work > 262144 || !blob_span(b, *at, 0)) return false;
    if (t == 1 || t == 2) {
        if (field) return true;
        if (*at >= end || (b->p[(size_t)*at] != 1 && b->p[(size_t)*at] != 2)) return false;
        ++*at;
        return true;
    }
    if (t == 3) n = 1;
    else if (t >= 4 && t <= 6) {
        if (!serialized_zig(b, at, end, &z) || (t == 4 && (z < -32768 || z > 32767)) || (t == 5 && (z < -2147483648LL || z > 2147483647LL))) return false;
        return true;
    } else if (t == 7) n = 8;
    else if (t == 13) n = 16;
    else if (t == 8) {
        if (!serialized_var(b, at, end, &n) || n > 16777216) return false;
    } else if (t == 9 || t == 10) {
        if (*at >= end) return false;
        c = b->p[(size_t)(*at)++];
        kind = c & 15;
        n = c >> 4;
        if (!kind || kind > 13 || (n == 15 && !serialized_var(b, at, end, &n)) || n > 65536) return false;
        for (v = 0; v < n; ++v)
            if (!tc_value(b, at, end, kind, depth + 1, work, false)) return false;
        return true;
    } else if (t == 11) {
        if (!serialized_var(b, at, end, &n) || n > 65536) return false;
        if (!n) return true;
        if (*at >= end) return false;
        c = b->p[(size_t)(*at)++];
        a = c >> 4;
        kind = c & 15;
        if (!a || a > 13 || !kind || kind > 13) return false;
        for (v = 0; v < n; ++v)
            if (!tc_value(b, at, end, a, depth + 1, work, false) || !tc_value(b, at, end, kind, depth + 1, work, false)) return false;
        return true;
    } else if (t == 12) return tc_struct(b, at, end, depth, work);
    else return false;
    if (!record_span(*at, n, end) || !blob_span(b, *at, n)) return false;
    *at += n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 0, n, v, start, name;
    unsigned work = 0, messages = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 8);
    while (at < b.n) {
        start = at;
        BLOB_NEED(blob_span(&b, at, 2) && b.p[(size_t)at] == 130 && (b.p[(size_t)at + 1] & 31) == 1 && (b.p[(size_t)at + 1] >> 5) >= 1 &&
                  (b.p[(size_t)at + 1] >> 5) <= 4);
        at += 2;
        BLOB_NEED(serialized_var(&b, &at, b.n, &v) && v <= 0xffffffffU && serialized_var(&b, &at, b.n, &n) && n && n <= 255 && serialized_utf(&b, at, n));
        name = at;
        at += n;
        BLOB_NEED(blob_add(f, s, &b, "rpc-header", start, name - start) && blob_add(f, s, &b, "method-name", name, n));
        start = at;
        BLOB_NEED(tc_struct(&b, &at, b.n, 0, &work) && at > start + 1 && blob_add(f, s, &b, "encoded-struct", start, at - start) && ++messages <= 256);
    }
    BLOB_NEED(messages);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_thrift_compact_init(xx_thrift_compact *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_THRIFT_COMPACT, "compact");
    }
}
xx_thrift_compact *xx_thrift_compact_create(xx_io_device *d, int64_t b)
{
    xx_thrift_compact *r = (xx_thrift_compact *)xx_mem_alloc(sizeof(*r));
    if (r) xx_thrift_compact_init(r, d, b);
    return r;
}
void xx_thrift_compact_destroy(xx_thrift_compact *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_thrift_compact_free(xx_thrift_compact *r)
{
    if (r) {
        xx_thrift_compact_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_thrift_compact_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_thrift_compact_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
