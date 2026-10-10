/* SPDX-License-Identifier: MIT. Original bounded Amiga archive carrier facts. */
#ifndef XX_ARCHIVE_WRAPPERS_H
#define XX_ARCHIVE_WRAPPERS_H
#include "xx_legacy_archive.h"
#include "xx_legacy_huffman.h"
#include "xxfclib/data/xx_data.h"
#include <string.h>
static XXFC_MAYBE_UNUSED bool aw_stub(const ac_blob *b, const uint32_t words[5], bool mxm)
{
    static const uint32_t offsets[2][5] = {{40, 48, 56, 60, 64}, {60, 68, 76, 80, 84}};
    if (b->n < 88 || xx_data_get_u32(b->p, 4, 0, true) != 0x3f3U) {
        return false;
    }
    for (unsigned i = 0; i < 5; ++i)
        if (xx_data_get_u32(b->p + offsets[mxm ? 1 : 0][i], 4, 0, true) != words[i]) return false;
    return true;
}
static bool aw_tail(const ac_blob *b, uint32_t at)
{
    unsigned pad = 0;
    for (;;) {
        if (at == b->n || (b->n - at == 4U && xx_data_get_u32(b->p + at, 4, 0, true) == 0x3f2U)) return true;
        if (at >= b->n || b->p[at] || pad++ == 3) return false;
        ++at;
    }
}
static bool aw_zoom(ac_blob *b, const uint8_t *p, uint32_t packed, uint8_t *out, uint32_t size)
{
    uint32_t wrote = 0, used = 0;
    return ac_adaptive(b, p, packed, out, size, &wrote, &used, 3U) && wrote == size && used == packed;
}
/* PCompress/Omni's LH block carries a raw CRC32 of compressed bytes. */
static XXFC_MAYBE_UNUSED bool aw_payload(Abstractformat *f, pm_stream *s, ac_blob *b, const char *name, uint32_t at, uint32_t packed)
{
    if (!ac_span(b, at, packed)) {
        return false;
    }
    if (packed >= 14U && b->p[at] == 'L' && b->p[at + 1] == 'H' && b->p[at + 2] == 0 && b->p[at + 6] == 0) {
        uint32_t size = xx_data_get_u32(b->p + at + 2, 4, 0, true), n = xx_data_get_u32(b->p + at + 6, 4, 0, true);
        uint8_t *out;
        if (n != packed - 14U || ac_crc32(b->p + at + 14, n, 0) != xx_data_get_u32(b->p + at + 10, 4, 0, true)) {
            return false;
        }
        out = ac_alloc(b, size);
        if (!out) return false;
        if (!aw_zoom(b, b->p + at + 14, n, out, size)) {
            ac_release(b, out, size);
            return false;
        }
        return ac_memory(f, s, b, name, out, size, packed, 1);
    }
    return ac_emit(f, s, b, name, at, packed);
}
static XXFC_MAYBE_UNUSED bool aw_medium(ac_blob *b, const uint8_t *p, uint32_t packed, uint8_t *out, uint32_t size)
{
    static const unsigned lengths[16] = {7, 7, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 11, 11, 11, 12};
    uint32_t offsets[16], sum = 0, at = 0, pos = 0x3fbe;
    uint8_t *ring;
    ac_bits bits = {p, packed, 0, false, false};
    for (unsigned i = 0; i < 16; ++i) {
        offsets[i] = sum;
        sum += 1U << lengths[i];
    }
    ring = ac_alloc(b, 16384);
    if (!ring) return false;
    memset(ring, 0, 16384);
    while (at < size) {
        uint32_t count = 1, distance = 0;
        uint8_t value;
        bool literal;
        if (!ac_poll(b)) goto fail;
        literal = ac_bits_get(&bits, 1) != 0;
        if (literal) value = (uint8_t)ac_bits_get(&bits, 8);
        else {
            unsigned index = ac_bits_get(&bits, 4);
            uint32_t symbol = offsets[index] + ac_bits_get(&bits, lengths[index]);
            index = (symbol >> 4) & 15U;
            distance = offsets[index] + ((symbol & 15U) << (lengths[index] - 4U)) + ac_bits_get(&bits, lengths[index] - 4U);
            count = (symbol >> 8) + 3U;
            value = 0;
        }
        if (bits.failed || count > size - at) {
            goto fail;
        }
        while (count--) {
            if (!literal) value = ring[(pos - distance - 1U) & 16383U];
            out[at++] = value;
            ring[pos++ & 16383U] = value;
        }
    }
    ac_release(b, ring, 16384);
    return !bits.failed && ((bits.bit + 7U) / 8U) == packed && ac_poll(b);
fail:
    ac_release(b, ring, 16384);
    return false;
}
#endif
