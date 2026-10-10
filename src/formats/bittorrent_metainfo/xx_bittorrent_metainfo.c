/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://www.bittorrent.org/beps/bep_0003.html */
#include "xxfclib/formats/bittorrent_metainfo/xx_bittorrent_metainfo.h"
#include "../common/xx_serialized_value_helpers.h"

static bool bt_string(memory_blob *b, uint64_t *at, uint64_t *start, uint64_t *n)
{
    uint64_t value = 0;
    unsigned digits = 0;
    if (!blob_span(b, *at, 1) || b->p[(size_t)*at] < '0' || b->p[(size_t)*at] > '9') return false;
    while (blob_span(b, *at, 1) && b->p[(size_t)*at] != ':') {
        uint8_t c = b->p[(size_t)(*at)++];
        if (c < '0' || c > '9' || (digits && !value) || ++digits > 9) return false;
        value = value * 10 + c - '0';
    }
    if (!blob_span(b, *at, 1)) return false;
    ++*at;
    *start = *at;
    *n = value;
    if (!blob_span(b, *at, value)) return false;
    *at += value;
    return true;
}
static bool bt_integer(memory_blob *b, uint64_t *at, int64_t *v)
{
    bool minus = false;
    unsigned digits = 0;
    uint64_t n = 0;
    if (!blob_span(b, *at, 1) || b->p[(size_t)(*at)++] != 'i' || !blob_span(b, *at, 1)) return false;
    if (b->p[(size_t)*at] == '-') {
        minus = true;
        ++*at;
    }
    while (blob_span(b, *at, 1) && b->p[(size_t)*at] != 'e') {
        uint8_t c = b->p[(size_t)(*at)++];
        if (c < '0' || c > '9' || (digits && !n) || ++digits > 19 || n > ((uint64_t)INT64_MAX - (c - '0')) / 10) return false;
        n = n * 10 + c - '0';
    }
    if (!digits || (minus && !n) || !blob_span(b, *at, 1)) return false;
    ++*at;
    *v = minus ? -(int64_t)n : (int64_t)n;
    return true;
}
static bool bt_value(memory_blob *b, uint64_t *at, unsigned depth, unsigned *work)
{
    uint64_t key, n, previous = 0, pn = 0;
    int64_t v;
    uint8_t c;
    if (depth > 32 || ++*work > 1048576 || !blob_span(b, *at, 1)) return false;
    c = b->p[(size_t)*at];
    if (c == 'i') return bt_integer(b, at, &v);
    if (c >= '0' && c <= '9') return bt_string(b, at, &key, &n);
    if (c != 'l' && c != 'd') return false;
    ++*at;
    while (blob_span(b, *at, 1) && b->p[(size_t)*at] != 'e') {
        if (c == 'd') {
            if (!bt_string(b, at, &key, &n) || !n || (pn && serialized_cmp(b->p + (size_t)previous, pn, b->p + (size_t)key, n) >= 0)) return false;
            previous = key;
            pn = n;
        }
        if (!bt_value(b, at, depth + 1, work)) return false;
    }
    if (!blob_span(b, *at, 1)) return false;
    ++*at;
    return true;
}
static bool bt_key(memory_blob *b, uint64_t at, uint64_t n, const char *text)
{
    return n == xx_rt_strlen(text) && !xx_rt_memcmp(b->p + (size_t)at, text, (size_t)n);
}
static bool bt_path(memory_blob *b, uint64_t *at)
{
    uint64_t key, n;
    unsigned count = 0;
    if (!blob_span(b, *at, 1) || b->p[(size_t)(*at)++] != 'l') return false;
    while (blob_span(b, *at, 1) && b->p[(size_t)*at] != 'e') {
        if (++count > 32 || !bt_string(b, at, &key, &n) || !serialized_utf(b, key, n) || !serialized_leaf(b->p + (size_t)key, n)) return false;
    }
    if (!count || !blob_span(b, *at, 1)) return false;
    ++*at;
    return true;
}
static bool bt_info(memory_blob *b, uint64_t *at, unsigned *work)
{
    uint64_t key, n, start, pieces = 0, piece = 0, total = 0, previous = 0, pn = 0;
    unsigned required = 0, files = 0;
    int64_t v;
    if (!blob_span(b, *at, 1) || b->p[(size_t)(*at)++] != 'd') return false;
    while (blob_span(b, *at, 1) && b->p[(size_t)*at] != 'e') {
        if (!bt_string(b, at, &key, &n) || !n || (pn && serialized_cmp(b->p + (size_t)previous, pn, b->p + (size_t)key, n) >= 0)) return false;
        previous = key;
        pn = n;
        if (bt_key(b, key, n, "name")) {
            if (!bt_string(b, at, &start, &n) || !serialized_utf(b, start, n) || !serialized_leaf(b->p + (size_t)start, n)) return false;
            required |= 1;
        } else if (bt_key(b, key, n, "piece length")) {
            if (!bt_integer(b, at, &v) || v <= 0 || v > 1073741824) return false;
            piece = (uint64_t)v;
            required |= 2;
        } else if (bt_key(b, key, n, "pieces")) {
            if (!bt_string(b, at, &start, &pieces) || pieces % 20) return false;
            required |= 4;
        } else if (bt_key(b, key, n, "length")) {
            if (required & 24 || !bt_integer(b, at, &v) || v < 0) return false;
            total = (uint64_t)v;
            required |= 8;
        } else if (bt_key(b, key, n, "files")) {
            if (required & 24 || !blob_span(b, *at, 1) || b->p[(size_t)(*at)++] != 'l') return false;
            required |= 16;
            while (blob_span(b, *at, 1) && b->p[(size_t)*at] != 'e') {
                uint64_t fk, fn, prev = 0, prevn = 0;
                unsigned fields = 0;
                if (++files > 2048 || b->p[(size_t)(*at)++] != 'd') return false;
                while (blob_span(b, *at, 1) && b->p[(size_t)*at] != 'e') {
                    if (!bt_string(b, at, &fk, &fn) || !fn || (prevn && serialized_cmp(b->p + (size_t)prev, prevn, b->p + (size_t)fk, fn) >= 0)) return false;
                    prev = fk;
                    prevn = fn;
                    if (bt_key(b, fk, fn, "length")) {
                        if (!bt_integer(b, at, &v) || v < 0 || (uint64_t)v > UINT64_MAX - total) return false;
                        total += (uint64_t)v;
                        fields |= 1;
                    } else if (bt_key(b, fk, fn, "path")) {
                        if (!bt_path(b, at)) return false;
                        fields |= 2;
                    } else if (!bt_value(b, at, 1, work)) return false;
                }
                if (fields != 3 || !blob_span(b, *at, 1)) return false;
                ++*at;
            }
            if (!files || !blob_span(b, *at, 1)) return false;
            ++*at;
        } else if (!bt_value(b, at, 1, work)) return false;
    }
    if ((required & 7) != 7 || !(required & 24) || !blob_span(b, *at, 1) || (total / piece + (total % piece != 0)) != pieces / 20) {
        return false;
    }
    ++*at;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 1, key, n, start, previous = 0, pn = 0, payload, len;
    unsigned work = 0, required = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(blob_span(&b, 0, 1) && b.p[0] == 'd');
    while (blob_span(&b, at, 1) && b.p[(size_t)at] != 'e') {
        start = at;
        BLOB_NEED(bt_string(&b, &at, &key, &n) && n && (!pn || serialized_cmp(b.p + (size_t)previous, pn, b.p + (size_t)key, n) < 0));
        previous = key;
        pn = n;
        if (bt_key(&b, key, n, "info")) {
            BLOB_NEED(bt_info(&b, &at, &work));
            required |= 1;
        } else if (bt_key(&b, key, n, "announce")) {
            BLOB_NEED(bt_string(&b, &at, &payload, &len) && len && serialized_utf(&b, payload, len));
            required |= 2;
        } else BLOB_NEED(bt_value(&b, &at, 0, &work));
        BLOB_NEED(blob_add(f, s, &b, "metainfo-entry", start, at - start));
    }
    BLOB_NEED(required == 3 && at + 1 == b.n && b.p[(size_t)at] == 'e');
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_bittorrent_metainfo_init(xx_bittorrent_metainfo *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_BITTORRENT_METAINFO, "torrent");
    }
}
xx_bittorrent_metainfo *xx_bittorrent_metainfo_create(xx_io_device *d, int64_t b)
{
    xx_bittorrent_metainfo *r = (xx_bittorrent_metainfo *)xx_mem_alloc(sizeof(*r));
    if (r) xx_bittorrent_metainfo_init(r, d, b);
    return r;
}
void xx_bittorrent_metainfo_destroy(xx_bittorrent_metainfo *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_bittorrent_metainfo_free(xx_bittorrent_metainfo *r)
{
    if (r) {
        xx_bittorrent_metainfo_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_bittorrent_metainfo_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_bittorrent_metainfo_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
