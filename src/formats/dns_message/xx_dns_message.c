/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://www.rfc-editor.org/rfc/rfc1035.html */
#include "xxfclib/formats/dns_message/xx_dns_message.h"
#include "../common/xx_container_wire_helpers.h"

static bool dn_name(memory_blob *b, uint64_t *at, uint64_t end, uint8_t *labels)
{
    uint64_t pos = *at, used = 0, visited = 0, resume = 0;
    unsigned labelCount = 0;
    bool jumped = false;
    while (pos < b->n) {
        uint8_t n;
        if (++visited > 256 || !blob_span(b, pos, 1) || (!jumped && pos >= end)) return false;
        if (!jumped) labels[(size_t)pos] = 1;
        n = b->p[(size_t)pos++];
        if ((n & 192) == 192) {
            uint64_t target;
            if (!blob_span(b, pos, 1) || (!jumped && pos >= end)) return false;
            target = ((uint64_t)(n & 63) << 8) | b->p[(size_t)pos++];
            if (target >= pos - 2 || target < 12 || !labels[(size_t)target]) return false;
            if (!jumped) resume = pos;
            jumped = true;
            pos = target;
            continue;
        }
        if (n & 192) return false;
        if (!n) {
            *at = jumped ? resume : pos;
            return used <= 254;
        }
        if (++labelCount > 127 || !record_span(pos, n, b->n) || (!jumped && !record_span(pos, n, end))) return false;
        used += n + 1;
        if (used > 254) return false;
        pos += n;
    }
    return false;
}
static bool dn_rdata(memory_blob *b, uint64_t at, uint64_t end, uint16_t type, uint8_t *labels)
{
    uint64_t n = end - at, p = at;
    if (type == 1) return n == 4;
    if (type == 28) return n == 16;
    if (type == 2 || type == 5 || type == 12) return dn_name(b, &p, end, labels) && p == end;
    if (type == 15 || type == 33) {
        unsigned h = type == 15 ? 2 : 6;
        if (n <= h) return false;
        p += h;
        return dn_name(b, &p, end, labels) && p == end;
    }
    if (type == 6) return dn_name(b, &p, end, labels) && dn_name(b, &p, end, labels) && end - p == 20;
    if (type == 16) {
        if (!n) return false;
        while (p < end) {
            uint8_t len = b->p[(size_t)p++];
            if (!record_span(p, len, end)) return false;
            p += len;
        }
        return p == end;
    }
    if (type == 41) {
        while (p < end) {
            if (!record_span(p, 4, end)) return false;
            n = xx_data_get_u16(b->p + (size_t)p + 2, 2, 0, true);
            p += 4;
            if (!record_span(p, n, end)) return false;
            p += n;
        }
        return p == end;
    }
    return false;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 12, start, n;
    uint16_t flags, counts[4];
    unsigned records = 0, opt = 0;
    uint8_t *labels = NULL;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 20 && b.n <= 65535 && blob_span(&b, 0, 12));
    labels = (uint8_t *)xx_mem_alloc((size_t)b.n);
    BLOB_NEED(labels);
    xx_mem_zero(labels, (size_t)b.n);
    flags = xx_data_get_u16(b.p + 2, 2, 0, true);
    BLOB_NEED(!(flags & 0x7840) && (flags & 15) <= 5);
    for (unsigned i = 0; i < 4; ++i) {
        counts[i] = xx_data_get_u16(b.p + 4 + i * 2, 2, 0, true);
        BLOB_NEED(counts[i] <= 256);
    }
    BLOB_NEED(counts[0] == 1 && counts[1] + counts[2] + counts[3] >= 1 && blob_add(f, s, &b, "dns-header", 0, 12));
    for (unsigned section = 0; section < 4; ++section)
        for (unsigned i = 0; i < counts[section]; ++i) {
            uint16_t type, cls;
            start = at;
            BLOB_NEED(dn_name(&b, &at, b.n, labels) && blob_span(&b, at, section ? 10U : 4U));
            type = xx_data_get_u16(b.p + (size_t)at, 2, 0, true);
            cls = xx_data_get_u16(b.p + (size_t)at + 2, 2, 0, true);
            BLOB_NEED(type && cls);
            if (!section) {
                BLOB_NEED(cls == 1 || cls == 255);
                at += 4;
            } else {
                n = xx_data_get_u16(b.p + (size_t)at + 8, 2, 0, true);
                at += 10;
                BLOB_NEED(record_span(at, n, b.n) &&
                          (type == 41
                               ? section == 3 && !opt++ && b.p[(size_t)start] == 0 && at - start == 11 && cls >= 512 &&
                                     !((xx_data_get_u32(b.p + (size_t)at - 6, 4, 0, true) >> 16) & 255U) && !(xx_data_get_u32(b.p + (size_t)at - 6, 4, 0, true) & 0x7fffU)
                               : cls == 1) &&
                          dn_rdata(&b, at, at + n, type, labels));
                at += n;
            }
            BLOB_NEED(++records <= 512 && blob_add(f, s, &b, section ? "resource-record" : "question", start, at - start));
        }
    BLOB_NEED(at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(labels);
    xx_mem_free(b.p);
    return ok;
}

void xx_dns_message_init(xx_dns_message *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_DNS_MESSAGE, "dns");
    }
}
xx_dns_message *xx_dns_message_create(xx_io_device *d, int64_t b)
{
    xx_dns_message *r = (xx_dns_message *)xx_mem_alloc(sizeof(*r));
    if (r) xx_dns_message_init(r, d, b);
    return r;
}
void xx_dns_message_destroy(xx_dns_message *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_dns_message_free(xx_dns_message *r)
{
    if (r) {
        xx_dns_message_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_dns_message_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_dns_message_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
