/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original Now Compress reader: authenticated fixed Macintosh directory,
 * block offset tables, canonical Huffman and two LZ stream grammars.
 * References: XADNowCompressParser.m/XADNowCompressHandle.m format facts.
 * Offset-repair heuristics are intentionally excluded. */
#include "xxfclib/formats/nowcompress/xx_nowcompress.h"
#include "../xx_legacy_archive.h"
#include "../xx_legacy_prefix.h"
static bool now_bits(const uint8_t *p, uint32_t n, unsigned endbits, ac_bits *bits, uint64_t *end)
{
    if (endbits > 16U || (endbits && n < 2U)) return false;
    bits->p = p;
    bits->n = n;
    bits->bit = 0;
    bits->failed = false;
    bits->lsb = false;
    *end = (uint64_t)n * 8U - (endbits ? 16U - endbits : 0U);
    return true;
}
static bool now_huffman(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t cap, unsigned symbols, uint32_t *written)
{
    uint8_t lengths[256] = {0};
    uint32_t pos = 1, i, count, at = 0;
    ac_prefix code;
    ac_bits bits;
    uint64_t end;
    if (!symbols || symbols > 256U || (symbols & 1U) || packed < 2U + symbols / 2U) return false;
    for (i = 0; i < symbols / 2U; ++i) {
        lengths[i * 2U] = in[pos] >> 4U;
        lengths[i * 2U + 1U] = in[pos++] & 15U;
    }
    count = in[pos++];
    if (count > packed - pos) return false;
    for (i = 0; i < count; ++i) {
        unsigned index = in[pos++];
        if (index >= symbols || lengths[index] >= 16U) return false;
        lengths[index] += 16U;
    }
    if (pos & 1U) ++pos;
    if (pos > packed || !ac_prefix_build(&code, lengths, symbols, 31U) || !now_bits(in + pos, packed - pos, in[0], &bits, &end)) return false;
    while (bits.bit < end) {
        unsigned token;
        if (at >= cap || !ac_poll(b) || !ac_prefix_read(&code, &bits, &token) || bits.bit > end) return false;
        out[at++] = (uint8_t)token;
    }
    *written = at;
    return bits.bit == end;
}
static bool now_old_lz(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t cap, uint32_t *written)
{
    uint32_t pos = 2, at = 0;
    unsigned flags = 0, left = 0;
    if (packed < 2U) return false;
    while (pos < packed) {
        uint32_t length, distance;
        uint8_t a, c;
        if (!ac_poll(b)) return false;
        if (!left) {
            flags = in[pos++];
            left = 8;
            if (pos >= packed) return false;
        }
        if (flags & 128U) {
            if (at == cap) return false;
            out[at++] = in[pos++];
        } else {
            if (packed - pos < 2U) return false;
            a = in[pos++];
            c = in[pos++];
            distance = ((uint32_t)(a & 0xF8U) << 5U) | c;
            length = a & 7U;
            if (!length) {
                if (pos >= packed) return false;
                length = in[pos++];
            }
            length += 2U;
            if (!distance || distance > at || length > cap - at) {
                return false;
            }
            while (length--) {
                out[at] = out[at - distance];
                ++at;
            }
        }
        flags <<= 1U;
        --left;
    }
    *written = at;
    return true;
}
static uint32_t now_length(unsigned selector, ac_bits *bits)
{
    unsigned extra;
    uint32_t base;
    if (selector < 16U) return selector + 2U;
    if (selector < 18U) {
        extra = 1;
        base = 16U + (selector - 16U) * 2U;
    } else if (selector < 23U) {
        extra = 2;
        base = 20U + (selector - 18U) * 4U;
    } else if (selector < 26U) {
        extra = 3;
        base = 40U + (selector - 23U) * 8U;
    } else if (selector < 30U) {
        extra = 4;
        base = 64U + (selector - 26U) * 16U;
    } else {
        extra = 5;
        base = 128U + (selector - 30U) * 32U;
    }
    return base + ac_bits_get(bits, extra) + 2U;
}
static uint32_t now_distance(unsigned selector, ac_bits *bits)
{
    unsigned extra;
    uint32_t base;
    if (selector < 8U) return selector;
    if (selector < 28U) {
        extra = selector / 4U - 1U;
        base = (8U << ((selector - 8U) / 4U)) + (selector % 4U) * (1U << extra);
    } else if (selector < 36U) {
        extra = 8;
        base = 256U + (selector - 28U) * 256U;
    } else {
        extra = 9U + (selector - 36U) / 4U;
        base = ((1U << extra) * 4U + 256U) + (selector % 4U) * (1U << extra);
    }
    return base + ac_bits_get(bits, extra);
}
static bool now_new_lz(ac_blob *b, const uint8_t *in, uint32_t packed, const uint8_t history[32768], uint8_t *out, uint32_t cap, uint32_t *written)
{
    uint8_t header[346], lengths[290];
    uint32_t header_size, pos, n, at = 0, i;
    uint64_t end;
    ac_prefix main, offsets;
    ac_bits bits;
    if (packed < 4U || xx_data_get_u16(in, 2, 0, true) < 0x2F59U) {
        return false;
    }
    header_size = xx_data_get_u16(in, 2, 0, true) - 0x2F59U;
    if (header_size > packed - 4U || !now_huffman(b, in + 4U, header_size, header, 346U, 20U, &n) || n != 346U) return false;
    pos = 4U + header_size;
    if (pos & 1U) ++pos;
    if (pos > packed) return false;
    for (i = 0; i < 290U; ++i) lengths[i] = header[i];
    if (!ac_prefix_build(&main, lengths, 290U, 20U)) return false;
    for (i = 0; i < 56U; ++i) {
        if (header[290U + i] && header[290U + i] < 2U) return false;
        lengths[i] = header[290U + i] ? header[290U + i] - 2U : 0;
    }
    if (!ac_prefix_build(&offsets, lengths, 56U, 20U) || !now_bits(in + pos, packed - pos, in[3], &bits, &end)) return false;
    while (bits.bit < end) {
        unsigned token;
        if (!ac_poll(b) || !ac_prefix_read(&main, &bits, &token)) return false;
        if (token < 256U) {
            if (at == cap) return false;
            out[at++] = (uint8_t)token;
        } else {
            uint32_t length = now_length(token - 256U, &bits), distance;
            unsigned selector;
            if (!ac_prefix_read(&offsets, &bits, &selector)) {
                return false;
            }
            distance = now_distance(selector, &bits);
            if (!distance || distance > 32768U + at || length > cap - at) return false;
            while (length--) {
                out[at] = distance > at ? history[32768U + at - distance] : out[at - distance];
                ++at;
            }
        }
        if (bits.failed || bits.bit > end) return false;
    }
    *written = at;
    return bits.bit == end;
}
static bool now_file(ac_blob *b, uint32_t start, uint32_t limit, uint8_t *out, uint32_t expected, uint8_t cache[32768])
{
    uint32_t first, entries, table, previous, i, sum = 0, produced = 0;
    uint8_t history[32768];
    bool initial = true;
    if (!ac_span(b, start, 20U)) {
        return false;
    }
    first = xx_data_get_u32(b->p + start, 4, 0, true);
    if (first < start + 20U || first > limit || (first - start - 4U) % 8U) return false;
    entries = (first - start - 4U) / 8U - 1U;
    if (!entries || entries > 8191U) return false;
    for (i = 0; i < 4U; ++i) {
        sum += b->p[start + i];
    }
    table = start + 4U;
    previous = first;
    xx_rt_memcpy(history, cache, 32768U);
    for (i = 0; i < entries; ++i) {
        uint32_t next, padding, packed, decoded = 0, j;
        unsigned flags;
        const uint8_t *p;
        if (!ac_span(b, table, 8U)) return false;
        for (j = 0; j < 8U; ++j) {
            sum += b->p[table + j];
        }
        flags = xx_data_get_u16(b->p + table, 2, 0, true);
        padding = xx_data_get_u16(b->p + table + 2U, 2, 0, true);
        next = xx_data_get_u32(b->p + table + 4U, 4, 0, true);
        table += 8U;
        if (next == previous) {
            initial = true;
            xx_rt_memcpy(history, cache, 32768U);
            continue;
        }
        if (next < previous || next > limit || next - previous < 4U + padding || flags & ~0x7FU) return false;
        packed = next - previous - padding - 4U;
        p = b->p + previous;
        if (flags & 0x20U) {
            if (flags & 0x1FU) {
                uint32_t cap = expected - produced;
                uint8_t *mid = ac_alloc(b, cap);
                uint32_t n = 0;
                bool ok;
                if (!mid) {
                    return false;
                }
                ok = now_huffman(b, p, packed, mid, cap, 256U, &n) && now_old_lz(b, mid, n, out + produced, cap, &decoded);
                ac_release(b, mid, cap);
                if (!ok) return false;
            } else if (!now_huffman(b, p, packed, out + produced, expected - produced, 256U, &decoded)) return false;
        } else if (flags & 0x40U) {
            if (!now_new_lz(b, p, packed, history, out + produced, expected - produced, &decoded)) return false;
        } else if (flags & 0x1FU) {
            if (!now_old_lz(b, p, packed, out + produced, expected - produced, &decoded)) return false;
        } else {
            decoded = packed;
            if (decoded > expected - produced) return false;
            xx_rt_memcpy(out + produced, p, decoded);
        }
        if (decoded >= 32768U) xx_rt_memcpy(history, out + produced + decoded - 32768U, 32768U);
        else {
            xx_rt_memmove(history, history + decoded, 32768U - decoded);
            xx_rt_memcpy(history + 32768U - decoded, out + produced, decoded);
        }
        if (initial) {
            xx_rt_memcpy(cache, history, 32768U);
            initial = false;
        }
        produced += decoded;
        previous = next;
    }
    if (!ac_span(b, table, 8U) || table + 8U != first) return false;
    for (i = 0; i < 4U; ++i) sum += b->p[table + i];
    return xx_data_get_u32(b->p + table + 4U, 4, 0, true) == sum && produced == expected && previous == limit;
}
static bool now_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t count, at = 24, i;
    uint8_t cache[32768] = {0};
    if (b->n < 134U || b->p[0] != 0U || b->p[1] != 2U || !(count = xx_data_get_u32(b->p + 8, 4, 0, true)) || count > 65535U || count > (b->n - 24U) / 110U) return false;
    for (i = 0; i < count; ++i, at += 110U) {
        uint32_t sum = 0, j, data, resource, start, end, n;
        char name[96], forkname[96];
        uint8_t *out;
        if (!ac_poll(b)) return false;
        for (j = 0; j < 106U; ++j) sum += b->p[at + j];
        if (sum != xx_data_get_u32(b->p + at + 106U, 4, 0, true) || !b->p[at] || b->p[at] > 31U || !ac_name(name, sizeof(name), b->p + at + 1U, b->p[at])) return false;
        if (xx_data_get_u16(b->p + at + 36U, 2, 0, true) & 0x10U) continue;
        data = xx_data_get_u32(b->p + at + 86U, 4, 0, true);
        resource = xx_data_get_u32(b->p + at + 90U, 4, 0, true);
        start = xx_data_get_u32(b->p + at + 98U, 4, 0, true);
        end = xx_data_get_u32(b->p + at + 102U, 4, 0, true);
        if (data > AC_MAX_BYTES || resource > AC_MAX_BYTES - data) {
            return false;
        }
        n = data + resource;
        if (!n) {
            if (!ac_emit(f, s, b, name, 0, 0)) return false;
            continue;
        }
        if (start < 24U + count * 110U || end < start || end > b->n) return false;
        out = ac_alloc(b, n);
        if (!out) return false;
        if (!now_file(b, start, end, out, n, cache)) {
            ac_release(b, out, n);
            return ac_error(b, "Now Compress offset table, checksum or decode failed");
        }
        if (resource) {
            uint8_t *fork = ac_alloc(b, resource);
            if (!fork) {
                ac_release(b, out, n);
                return false;
            }
            xx_rt_memcpy(fork, out, resource);
            xx_rt_snprintf(forkname, sizeof(forkname), "%s.resource", name);
            if (!ac_memory(f, s, b, forkname, fork, resource, end - start, 1)) {
                ac_release(b, out, n);
                return false;
            }
            xx_rt_memmove(out, out + resource, data);
        }
        if (data || !resource) {
            if (!ac_compact(b, &out, n, data)) {
                ac_release(b, out, n);
                return false;
            }
            if (!ac_memory(f, s, b, name, out, data, end - start, 1)) return false;
        } else ac_release(b, out, n);
    }
    ((xx_nowcompress *)f)->note = "data/resource fork payloads; original name leaves are prefixed for safe collision-free output; no heuristic offset repair";
    return s->count != 0;
}
AC_PARSE(now_parse)
AC_DEFINE(nowcompress, XX_FILE_TYPE_NOWCOMPRESS, "now")
