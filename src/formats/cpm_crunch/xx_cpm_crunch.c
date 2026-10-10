/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * CP/M Crunch/CRLZH 76FE/76FD single-file wrapper. Independent bounded
 * dictionary implementations from the published code-table/hash facts;
 * the adaptive model is based on xxfclib's existing MIT LH implementation.
 * References: XADCrunchParser.m and XADCrunchHandles.m format descriptions. */
#include "xxfclib/formats/cpm_crunch/xx_cpm_crunch.h"
#include "../xx_legacy_archive.h"
#include "../xx_legacy_huffman.h"
#define CR_EMPTY 0x8000U
#define CR_NONE 0x3FFFU
typedef struct cr_dictionary {
    uint16_t parent[4096], hash[5003];
    uint8_t suffix[4096];
    unsigned next, width;
} cr_dictionary;
static bool cr_insert_old(cr_dictionary *d, unsigned parent, uint8_t suffix)
{
    unsigned hash, base, steps = 0, a = ((parent + suffix) | 0x800U) & 0x1FFFU;
    hash = parent == CR_NONE && suffix == 0 ? 0x800U : (((a >> 1U) * ((a >> 1U) + (a & 1U))) >> 4U) & 4095U;
    while (d->hash[hash] != CR_EMPTY) {
        hash = d->hash[hash];
        if (hash >= 4096U || ++steps > 4096U) return false;
    }
    if (d->parent[hash] != CR_EMPTY) {
        base = hash;
        hash = (hash + 101U) & 4095U;
        steps = 0;
        while (d->parent[hash] != CR_EMPTY) {
            hash = (hash + 1U) & 4095U;
            if (++steps >= 4096U) return false;
        }
        d->hash[base] = (uint16_t)hash;
    }
    d->parent[hash] = (uint16_t)parent;
    d->suffix[hash] = suffix;
    ++d->next;
    return true;
}
static unsigned cr_hash(unsigned parent, uint8_t suffix)
{
    return ((((parent >> 4U) & 255U) ^ suffix) | ((parent & 15U) << 8U)) + 1U;
}
static bool cr_insert(cr_dictionary *d, unsigned parent, uint8_t suffix)
{
    unsigned slot = cr_hash(parent, suffix), step = slot, guard = 0;
    if (d->next >= 4096U) return false;
    while (d->hash[slot] != CR_EMPTY) {
        slot = (slot + step) % 5003U;
        if (++guard >= 5003U) return false;
    }
    d->hash[slot] = (uint16_t)d->next;
    d->parent[d->next] = (uint16_t)parent;
    d->suffix[d->next++] = suffix;
    if (d->width < 12U && d->next >= (1U << d->width) - 1U) ++d->width;
    return true;
}
static bool cr_initialize(cr_dictionary *d, bool old)
{
    unsigned i;
    d->next = 0;
    d->width = 9;
    for (i = 0; i < 5003U; ++i) d->hash[i] = CR_EMPTY;
    for (i = 0; i < 4096U; ++i) d->parent[i] = CR_EMPTY;
    if (old) {
        d->parent[0] = CR_NONE;
        for (i = 0; i < 256U; ++i)
            if (!cr_insert_old(d, CR_NONE, (uint8_t)i)) return false;
    } else {
        for (i = 0; i < 256U; ++i)
            if (!cr_insert(d, CR_NONE, (uint8_t)i)) return false;
        for (i = 0; i < 4U; ++i)
            if (!cr_insert(d, 0x7FFFU, 0)) return false;
    }
    return true;
}
static bool cr_lzw(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t cap, uint32_t *written, uint32_t *consumed, bool old)
{
    cr_dictionary d;
    ac_bits bits;
    uint8_t stack[4096], first = 0;
    unsigned previous = CR_NONE, at = 0;
    if (!cr_initialize(&d, old)) return false;
    bits.p = in;
    bits.n = packed;
    bits.bit = 0;
    bits.failed = false;
    bits.lsb = false;
    for (;;) {
        unsigned token = ac_bits_get(&bits, old ? 12U : d.width), code = token, top = 0, guard = 0;
        bool exceptional = false;
        if (bits.failed || !ac_poll(b)) return false;
        if (token == (old ? 0U : 256U)) break;
        if (!old && token == 257U) {
            if (!cr_initialize(&d, false)) return false;
            previous = CR_NONE;
            continue;
        }
        if (!old && (token == 258U || token == 259U)) continue;
        if ((old && d.parent[token] == CR_EMPTY) || (!old && token >= d.next)) {
            if (previous == CR_NONE || (!old && token != d.next)) return false;
            stack[top++] = first;
            code = previous;
            exceptional = true;
        }
        while (d.parent[code] != CR_NONE) {
            unsigned parent = d.parent[code] & 4095U;
            if (top >= 4095U || ++guard >= 4096U || d.parent[code] == CR_EMPTY || (!old && (code < 260U || parent >= d.next))) return false;
            stack[top++] = d.suffix[code];
            code = parent;
        }
        first = d.suffix[code];
        stack[top++] = first;
        if (top > cap - at) {
            return false;
        }
        while (top) out[at++] = stack[--top];
        if (previous != CR_NONE) {
            if (old) {
                if (d.next < 4095U && !cr_insert_old(&d, previous, first)) return false;
            } else if (d.next < 4096U) {
                if (!cr_insert(&d, previous, first)) return false;
            } else {
                unsigned slot = cr_hash(previous, first), step = slot;
                for (guard = 0; guard < 5003U && d.hash[slot] != CR_EMPTY; ++guard, slot = (slot + step) % 5003U) {
                    unsigned entry = d.hash[slot];
                    if (entry >= 260U && !(d.parent[entry] & 0x2000U)) {
                        d.parent[entry] = (uint16_t)previous;
                        d.suffix[entry] = first;
                        break;
                    }
                }
            }
        }
        if (!old) {
            if (exceptional) token = d.next - 1U;
            d.parent[token] |= 0x2000U;
        }
        previous = token;
    }
    *written = at;
    *consumed = (uint32_t)((bits.bit + 7U) / 8U);
    return ac_poll(b);
}
static bool crunch_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t at = 2, start, n, used, sum = 0, i, cap;
    uint8_t *out;
    char name[96];
    bool old;
    unsigned type;
    if (b->n < 11U || b->p[0] != 0x76U || (b->p[1] != 0xFEU && b->p[1] != 0xFDU)) return false;
    type = b->p[1];
    while (at < b->n && b->p[at] && at < 95U) ++at;
    if (!ac_span(b, at, 5U) || !ac_name(name, sizeof(name), b->p + 2, at - 2U)) return false;
    ++at;
    if (b->p[at] < 0x10U || b->p[at] > 0x2FU || b->p[at + 1U] < 0x10U || b->p[at + 1U] > 0x2FU || b->p[at + 2U] > 1U || b->p[at + 3U]) return false;
    old = (b->p[at + 1U] & 0xF0U) == 0x10U;
    start = at + 4U;
    cap = 8U * 1024U * 1024U;
    {
        const xx_var *v = ac_option(f, XX_META_ID_OPT_MAX_MEMBER_SIZE);
        if (v && xx_var_get_u64(v) < cap) cap = (uint32_t)xx_var_get_u64(v);
    }
    if (b->used >= b->limit) {
        return false;
    }
    if (cap > b->limit - b->used) cap = (uint32_t)(b->limit - b->used);
    out = ac_alloc(b, cap);
    if (!out) return false;
    if (type == 0xFEU ? !cr_lzw(b, b->p + start, b->n - start, out, cap, &n, &used, old)
                      : !ac_adaptive(b, b->p + start, b->n - start, out, cap, &n, &used, old ? 1U : 2U)) {
        ac_release(b, out, cap);
        return false;
    }
    /* Crunch LZW applies RLE90 type2 (count1 is also a literal90).
     * CRLZH's adaptive stream emits the plaintext directly. */
    if (type == 0xFEU) {
        uint8_t *filtered = ac_alloc(b, cap);
        uint32_t src = 0, dst = 0;
        if (!filtered) {
            ac_release(b, out, cap);
            return false;
        }
        while (src < n) {
            uint32_t count = 1;
            uint8_t c = out[src++];
            if (c == 0x90U) {
                if (src == n) {
                    ac_release(b, filtered, cap);
                    ac_release(b, out, cap);
                    return false;
                }
                count = out[src++];
                if (count <= 1U) {
                    c = 0x90U;
                    count = 1;
                } else {
                    if (!dst) {
                        ac_release(b, filtered, cap);
                        ac_release(b, out, cap);
                        return false;
                    }
                    c = filtered[dst - 1U];
                    --count;
                }
            }
            if (count > cap - dst) {
                ac_release(b, filtered, cap);
                ac_release(b, out, cap);
                return false;
            }
            while (count--) filtered[dst++] = c;
        }
        ac_release(b, out, cap);
        out = filtered;
        n = dst;
    }
    if (b->p[at + 2U] == 0U) {
        if (!ac_span(b, start + used, 2U)) {
            ac_release(b, out, cap);
            return false;
        }
        for (i = 0; i < n; ++i) sum += out[i];
        if ((uint16_t)sum != xx_data_get_u16(b->p + start + used, 2, 0, false)) {
            ac_release(b, out, cap);
            return ac_error(b, "CP/M Crunch checksum mismatch");
        }
        used += 2U;
    }
    if (b->n - start - used > 127U) {
        ac_release(b, out, cap);
        return false;
    }
    if (!ac_compact(b, &out, cap, n)) {
        ac_release(b, out, cap);
        return false;
    }
    return ac_memory(f, s, b, name, out, n, used, (uint16_t)type);
}
AC_PARSE(crunch_parse)
AC_DEFINE(cpm_crunch, XX_FILE_TYPE_CPM_CRUNCH, "crn")
