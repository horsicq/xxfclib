/* SPDX-License-Identifier: MIT. private typed framing primitives. */
#ifndef XX_NETWORK_PACKET_H
#define XX_NETWORK_PACKET_H
#include "xx_security_framing.h"
static bool packet_ascii(memory_blob *b, uint64_t at, uint64_t n, bool space) {
    if (!n || n > 65536 || !blob_span(b, at, n))
        return false;
    for (uint64_t i = 0; i < n; ++i) {
        uint8_t c = b->p[(size_t)(at + i)];
        if (c < (space ? 32 : 33) || c > 126)
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool packet_utf(memory_blob *b, uint64_t at, uint64_t n) {
    if (!n || n > 65536 || !serialized_utf(b, at, n))
        return false;
    for (uint64_t i = 0; i < n; ++i)
        if (!b->p[(size_t)(at + i)] || b->p[(size_t)(at + i)] == 127 || b->p[(size_t)(at + i)] < 32)
            return false;
    return true;
}
static uint32_t packet_sum(memory_blob *b, uint64_t at, uint64_t n, uint32_t sum) {
    if (!blob_span(b, at, n))
        return UINT32_MAX;
    for (uint64_t i = 0; i < n; i += 2) {
        if ((i & 65535) == 0 && binary_stop(b->pd))
            return UINT32_MAX;
        sum += (uint32_t)b->p[(size_t)(at + i)] << 8;
        if (i + 1 < n)
            sum += b->p[(size_t)(at + i + 1)];
        sum = (sum & 65535) + (sum >> 16);
    }
    return sum;
}
static bool packet_checksum(memory_blob *b, uint64_t at, uint64_t n, uint32_t sum) {
    sum = packet_sum(b, at, n, sum);
    if (sum == UINT32_MAX)
        return false;
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return sum == 65535;
}
static bool packet_ip(memory_blob *b, uint64_t start, uint64_t n, uint64_t *header) {
    uint64_t h;
    uint32_t sum = 0;
    unsigned version;
    if (n < 28 || n > 65575 || !blob_span(b, start, n))
        return false;
    version = b->p[(size_t)start] >> 4;
    if (version == 4) {
        h = (uint64_t)(b->p[(size_t)start] & 15) * 4;
        if (h < 20 || h > 60 || h + 8 >= n || xx_data_get_u16(b->p + (size_t)start + 2, 2, 0, true) != n ||
            (xx_data_get_u16(b->p + (size_t)start + 6, 2, 0, true) & 0xbfff) || !b->p[(size_t)start + 8] ||
            b->p[(size_t)start + 9] != 17 || !packet_checksum(b, start, h, 0))
            return false;
        uint64_t p = start + 20;
        while (p < start + h) {
            unsigned t = b->p[(size_t)p++];
            if (!t) {
                if (!blob_zero(b, p, start + h - p))
                    return false;
                break;
            }
            if (t == 1)
                continue;
            if (p == start + h)
                return false;
            unsigned z = b->p[(size_t)p++];
            if (z < 2 || !protocol_take(b, &p, start + h, z - 2))
                return false;
        }
        sum = packet_sum(b, start + 12, 8, 0);
    } else if (version == 6) {
        h = 40;
        if (n <= 48 || xx_data_get_u16(b->p + (size_t)start + 4, 2, 0, true) != n - 40 ||
            b->p[(size_t)start + 6] != 17 || !b->p[(size_t)start + 7])
            return false;
        sum = packet_sum(b, start + 8, 32, 0);
    } else
        return false;
    uint64_t u = start + h, un = n - h;
    if (xx_data_get_u16(b->p + (size_t)u + 4, 2, 0, true) != un || !xx_data_get_u16(b->p + (size_t)u, 2, 0, true) ||
        !xx_data_get_u16(b->p + (size_t)u + 2, 2, 0, true) || !xx_data_get_u16(b->p + (size_t)u + 6, 2, 0, true))
        return false;
    sum += 17 + (uint32_t)un;
    if (!packet_checksum(b, u, un, sum))
        return false;
    *header = h;
    return true;
}
static XXFC_MAYBE_UNUSED bool packet_ip_add(Abstractformat *f, pm_stream *s, memory_blob *b, uint64_t start,
                                            uint64_t n) {
    uint64_t h;
    if (!packet_ip(b, start, n, &h))
        return false;
    return blob_add(f, s, b, "ip-header", start, h) && blob_add(f, s, b, "udp-header", start + h, 8) &&
           blob_add(f, s, b, "encoded-udp-payload", start + h + 8, n - h - 8);
}
static XXFC_MAYBE_UNUSED bool packet_decoded(Abstractformat *f, pm_stream *s, const char *label, uint8_t **p,
                                             uint64_t n) {
    if (!*p || !n || s->count >= 4096)
        return false;
    return protocol_mem(f, s, label, p, n);
}

static XXFC_MAYBE_UNUSED bool packet_b64(memory_blob *b, uint64_t at, uint64_t n, bool url, uint8_t **out,
                                         uint64_t *bytes) {
    uint8_t *tmp = NULL;
    memory_blob view;
    uint64_t padded = (n + 3) & ~3ULL;
    bool ok = false;
    if (!n || n % 4 == 1 || n > 1048576 || !blob_span(b, at, n))
        return false;
    tmp = (uint8_t *)xx_mem_alloc((size_t)padded);
    if (!tmp)
        return false;
    for (uint64_t i = 0; i < n; ++i) {
        uint8_t c = b->p[(size_t)(at + i)];
        if (binary_stop(b->pd))
            goto done;
        if (url) {
            if (c == '-')
                c = '+';
            else if (c == '_')
                c = '/';
            else if (c == '+' || c == '/')
                goto done;
        }
        if (security_b64c(c) < 0)
            goto done;
        tmp[i] = c;
    }
    for (uint64_t i = n; i < padded; ++i)
        tmp[i] = '=';
    view.p = tmp;
    view.n = padded;
    view.pd = b->pd;
    ok = security_b64(&view, 0, padded, out, bytes);
done:
    xx_mem_free(tmp);
    return ok;
}
static XXFC_MAYBE_UNUSED bool packet_zstr(memory_blob *b, uint64_t at, uint64_t n, uint64_t *len) {
    uint64_t p = 0;
    if (!blob_span(b, at, n))
        return false;
    while (p < n && b->p[(size_t)(at + p)])
        ++p;
    if (!p || p == n || !packet_ascii(b, at, p, true) || !blob_zero(b, at + p, n - p))
        return false;
    *len = p;
    return true;
}
#endif
