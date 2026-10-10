/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://www.rfc-editor.org/rfc/rfc3550.html */
#include "xxfclib/formats/rtp_rtcp/xx_rtp_rtcp.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

static bool rtcp(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    uint64_t at = 0;
    unsigned packets = 0;
    bool cname = false;
    while (at < b->n) {
        uint64_t start = at;
        unsigned items, type, pad;
        uint64_t end, p, n;
        if (++packets > 256 || !protocol_take(b, &at, b->n, 4)) return false;
        items = b->p[(size_t)start] & 31;
        type = b->p[(size_t)start + 1];
        pad = (b->p[(size_t)start] >> 5) & 1;
        n = ((uint64_t)xx_data_get_u16(b->p + (size_t)start + 2, 2, 0, true) + 1) * 4;
        if ((b->p[(size_t)start] >> 6) != 2 || !blob_span(b, start, n) || n < 4) return false;
        end = start + n;
        if (pad) {
            unsigned z = b->p[(size_t)end - 1];
            if (end != b->n || !z || z > n - 4) return false;
            end -= z;
        }
        p = start + 4;
        if (packets == 1 && type != 200 && type != 201) return false;
        if (type == 200 || type == 201) {
            uint64_t fixed = type == 200 ? 24 : 4;
            if (end - p != fixed + (uint64_t)items * 24) return false;
        } else if (type == 202) {
            if (!items) return false;
            for (unsigned k = 0; k < items; ++k) {
                if (!protocol_take(b, &p, end, 4)) return false;
                unsigned fields = 0;
                bool term = false;
                while (p < end) {
                    unsigned t = b->p[(size_t)p++];
                    if (!t) {
                        term = true;
                        break;
                    }
                    if (t > 7 || ++fields > 256 || !protocol_take(b, &p, end, 1)) return false;
                    unsigned z = b->p[(size_t)p - 1];
                    uint64_t value = p;
                    if (!protocol_take(b, &p, end, z) || !z || !serialized_utf(b, value, z)) return false;
                    if (t == 1) cname = true;
                }
                if (!term) return false;
                while ((p - start) & 3) {
                    if (!protocol_take(b, &p, end, 1) || b->p[(size_t)p - 1]) return false;
                }
            }
            if (p != end) return false;
        } else if (type == 203) {
            if (!items || !protocol_take(b, &p, end, (uint64_t)items * 4)) return false;
            if (p < end) {
                if (!protocol_take(b, &p, end, 1)) return false;
                unsigned z = b->p[(size_t)p - 1];
                uint64_t value = p;
                if (!protocol_take(b, &p, end, z) || !serialized_utf(b, value, z) || end - p > 3 || !blob_zero(b, p, end - p)) return false;
            }
        } else if (type == 204) {
            if (end - p < 8) return false;
        } else return false;
        if (!blob_add(f, s, b, "rtcp-packet", start, n)) return false;
        at = start + n;
    }
    return packets >= 2 && cname;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 12 && (b.p[0] >> 6) == 2);
    if (b.p[1] >= 192 && b.p[1] <= 223) {
        BLOB_NEED(rtcp(f, s, &b));
    } else {
        unsigned csrc = b.p[0] & 15;
        BLOB_NEED((b.p[1] & 127) >= 96 && csrc > 0 && (b.p[0] & 16));
        uint64_t at = 12 + (uint64_t)csrc * 4, end = b.n;
        BLOB_NEED(blob_span(&b, 0, at) && blob_add(f, s, &b, "rtp-header-csrc", 0, at));
        if (b.p[0] & 16) {
            uint64_t start = at;
            BLOB_NEED(protocol_take(&b, &at, end, 4));
            uint64_t n = (uint64_t)xx_data_get_u16(b.p + (size_t)start + 2, 2, 0, true) * 4;
            BLOB_NEED(n > 0 && n <= 4096 && protocol_take(&b, &at, end, n) && blob_add(f, s, &b, "rtp-extension", start, n + 4));
        }
        if (b.p[0] & 32) {
            unsigned n = b.p[(size_t)end - 1];
            BLOB_NEED(n > 0 && n <= end - at);
            end -= n;
            BLOB_NEED(blob_add(f, s, &b, "rtp-padding", end, n));
        }
        BLOB_NEED(at < end && blob_add(f, s, &b, "encoded-media", at, end - at));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_rtp_rtcp_init(xx_rtp_rtcp *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_RTP_RTCP, "bin");
    }
}
xx_rtp_rtcp *xx_rtp_rtcp_create(xx_io_device *d, int64_t b)
{
    xx_rtp_rtcp *r = (xx_rtp_rtcp *)xx_mem_alloc(sizeof(*r));
    if (r) xx_rtp_rtcp_init(r, d, b);
    return r;
}
void xx_rtp_rtcp_destroy(xx_rtp_rtcp *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_rtp_rtcp_free(xx_rtp_rtcp *r)
{
    if (r) {
        xx_rtp_rtcp_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_rtp_rtcp_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_rtp_rtcp_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
