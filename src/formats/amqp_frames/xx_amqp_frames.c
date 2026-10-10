/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/rabbitmq/amqp-0.9.1-spec/blob/main/xml/amqp0-9-1.xml */
#include "xxfclib/formats/amqp_frames/xx_amqp_frames.h"
#include "../common/xx_container_wire_helpers.h"

static bool aq_short(memory_blob *b, uint64_t *at, uint64_t end, bool empty)
{
    uint64_t n;
    if (*at >= end) return false;
    n = b->p[(size_t)(*at)++];
    if ((!empty && !n) || !record_span(*at, n, end) || !serialized_utf(b, *at, n)) return false;
    *at += n;
    return true;
}
static bool aq_fields(memory_blob *b, uint64_t *at, uint64_t end, unsigned depth, unsigned *work, bool array);
static bool aq_field(memory_blob *b, uint64_t *at, uint64_t end, unsigned depth, unsigned *work)
{
    uint8_t t;
    uint64_t n;
    if (depth > 32 || ++*work > 262144 || *at >= end) return false;
    t = b->p[(size_t)(*at)++];
    if (t == 'F' || t == 'A') return aq_fields(b, at, end, depth + 1, work, t == 'A');
    if (t == 't' || t == 'b' || t == 'B') n = 1;
    else if (t == 'U' || t == 'u') n = 2;
    else if (t == 'I' || t == 'i' || t == 'f') n = 4;
    else if (t == 'L' || t == 'l' || t == 'd' || t == 'T') n = 8;
    else if (t == 'D') n = 5;
    else if (t == 'V') n = 0;
    else if (t == 's') return aq_short(b, at, end, true);
    else if (t == 'S' || t == 'x') {
        if (!record_span(*at, 4, end)) return false;
        n = xx_data_get_u32(b->p + (size_t)*at, 4, 0, true);
        *at += 4;
        if (n > 16777216) return false;
    } else return false;
    if (!record_span(*at, n, end) || !blob_span(b, *at, n) || (t == 't' && b->p[(size_t)*at] > 1)) return false;
    *at += n;
    return true;
}
static bool aq_fields(memory_blob *b, uint64_t *at, uint64_t end, unsigned depth, unsigned *work, bool array)
{
    uint64_t n, stop;
    if (!record_span(*at, 4, end)) return false;
    n = xx_data_get_u32(b->p + (size_t)*at, 4, 0, true);
    *at += 4;
    if (!record_span(*at, n, end)) return false;
    stop = *at + n;
    while (*at < stop) {
        if ((!array && !aq_short(b, at, stop, false)) || !aq_field(b, at, stop, depth + 1, work)) return false;
    }
    return *at == stop;
}
static bool aq_properties(memory_blob *b, uint64_t *at, uint64_t end, unsigned *work)
{
    uint16_t flags;
    if (!record_span(*at, 2, end)) return false;
    flags = xx_data_get_u16(b->p + (size_t)*at, 2, 0, true);
    *at += 2;
    if (flags & 3) return false;
    for (unsigned bit = 15; bit >= 2; --bit)
        if (flags & (1U << bit)) {
            if (bit == 13) {
                if (!aq_fields(b, at, end, 0, work, false)) return false;
            } else if (bit == 12 || bit == 11) {
                if (*at >= end || (bit == 12 ? b->p[(size_t)*at] < 1 || b->p[(size_t)*at] > 2 : b->p[(size_t)*at] > 9)) return false;
                ++*at;
            } else if (bit == 6) {
                if (!record_span(*at, 8, end)) return false;
                *at += 8;
            } else if (!aq_short(b, at, end, true)) return false;
        }
    return *at == end;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 8, start, end, n, p, body = 0, read = 0;
    uint16_t pending = 0;
    unsigned phase = 0, publishes = 0, frames = 0, work = 0;
    bool closed = false, ok = false;
    static const uint8_t magic[] = {'A', 'M', 'Q', 'P', 0, 0, 9, 1};
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 40 && !xx_rt_memcmp(b.p, magic, 8) && blob_add(f, s, &b, "amqp-protocol", 0, 8));
    while (at < b.n) {
        uint8_t t;
        uint16_t channel;
        BLOB_NEED(blob_span(&b, at, 8) && !closed);
        start = at;
        t = b.p[(size_t)at];
        channel = xx_data_get_u16(b.p + (size_t)at + 1, 2, 0, true);
        n = xx_data_get_u32(b.p + (size_t)at + 3, 4, 0, true);
        at += 7;
        BLOB_NEED(n <= 33554432 && blob_span(&b, at, n + 1));
        end = at + n;
        BLOB_NEED(b.p[(size_t)end] == 206);
        p = at;
        if (t == 1) {
            uint16_t cls, method;
            BLOB_NEED(!phase && n >= 4);
            cls = xx_data_get_u16(b.p + (size_t)p, 2, 0, true);
            method = xx_data_get_u16(b.p + (size_t)p + 2, 2, 0, true);
            p += 4;
            if (cls == 20 && method == 10) {
                BLOB_NEED(channel && p < end && !b.p[(size_t)p] && aq_short(&b, &p, end, true));
            } else if (cls == 60 && method == 40) {
                BLOB_NEED(channel && record_span(p, 2, end) && !xx_data_get_u16(b.p + (size_t)p, 2, 0, true));
                p += 2;
                BLOB_NEED(aq_short(&b, &p, end, true) && aq_short(&b, &p, end, false) && p + 1 == end && b.p[(size_t)p] <= 3);
                ++p;
                pending = channel;
                phase = 1;
                ++publishes;
            } else if (cls == 10 && method == 50) {
                BLOB_NEED(!channel && record_span(p, 2, end));
                p += 2;
                BLOB_NEED(aq_short(&b, &p, end, true) && end - p == 4);
                p += 4;
                closed = true;
            } else BLOB_NEED(false);
            BLOB_NEED(p == end && blob_add(f, s, &b, "method-frame", start, n + 8));
        } else if (t == 2) {
            BLOB_NEED(phase == 1 && channel == pending && n >= 14 && xx_data_get_u16(b.p + (size_t)p, 2, 0, true) == 60 &&
                      !xx_data_get_u16(b.p + (size_t)p + 2, 2, 0, true));
            body = xx_data_get_u64(b.p + (size_t)p + 4, 8, 0, true);
            BLOB_NEED(body <= 33554432);
            p += 12;
            BLOB_NEED(aq_properties(&b, &p, end, &work) && blob_add(f, s, &b, "content-header", start, n + 8));
            read = 0;
            phase = body ? 2U : 0U;
        } else if (t == 3) {
            BLOB_NEED(phase == 2 && channel == pending && n && n <= body - read && blob_add(f, s, &b, "frame-prefix", start, 7) && blob_add(f, s, &b, "body", at, n) &&
                      blob_add(f, s, &b, "frame-end", end, 1));
            read += n;
            if (read == body) phase = 0;
        } else {
            if (t == 8) BLOB_NEED(!channel && !n && blob_add(f, s, &b, "heartbeat", start, 8));
            else BLOB_NEED(false);
        }
        at = end + 1;
        BLOB_NEED(++frames <= 1024);
    }
    BLOB_NEED(closed && publishes && !phase && frames >= 4);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_amqp_frames_init(xx_amqp_frames *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AMQP_FRAMES, "amqp");
    }
}
xx_amqp_frames *xx_amqp_frames_create(xx_io_device *d, int64_t b)
{
    xx_amqp_frames *r = (xx_amqp_frames *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amqp_frames_init(r, d, b);
    return r;
}
void xx_amqp_frames_destroy(xx_amqp_frames *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amqp_frames_free(xx_amqp_frames *r)
{
    if (r) {
        xx_amqp_frames_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amqp_frames_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_amqp_frames_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
