/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * CADC .CAR archives: the document containers of IBM's IPF (Information
 * Presentation Facility) tooling and demos (IPFE, caddt, cadcdemo, SOUNDDEM).
 * Members follow each other without an archive header:
 *   u8 0x03, u8 0x01   record tag
 *   char name[]        NUL-terminated
 *   u8   method        1 = stored, 2 = LZSS
 *   u32  original size, u32 packed size
 *   u8   subheader[12] u32 CRC-32 of the original data (reflected
 *                      0xEDB88320), u32 unknown, u16 DOS date, u16 DOS time
 *   packed bytes
 * A lone 0x00 in place of the next tag ends the archive. Method 2 is the
 * byte-oriented flag-byte LZSS that CARMAN method 2 uses as well: a flag
 * byte before every eight tokens read from bit 0 up, a set bit one literal
 * byte, a clear bit a two-byte match [b0, b1] with length (b0 >> 4) + 2 and
 * the ABSOLUTE position ((b0 & 15) << 8) | b1 in a 4096-byte ring that
 * starts zeroed with the write pointer at 1.
 * Recovered 2026-11-08 from the samples; the stored CRC-32 verifies on 221
 * of 221 members, which is what makes the two-byte tag safely identifiable. */
#include "xxfclib/formats/archive/cadc/xx_cadc.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#define CD_NAME_MAX 95U
#define CD_FIXED 21U /* method, two sizes, the 12-byte subheader */
#define CD_TOTAL INT64_C(1073741824)
#define CD_WINDOW 4096U
#define CD_MAX_MEMBER INT64_C(268435456)
#define CD_METHOD_STORED 1U
#define CD_METHOD_LZSS 2U

typedef struct cd_member {
    uint32_t crc; /**< CRC-32 the original data must have */
} cd_member;

static uint32_t cd_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Printable bytes only; the unpacker's own name policy handles slashes. */
static bool cd_name(const uint8_t *p, size_t n, char name[96])
{
    size_t i;
    if (!n || n > CD_NAME_MAX) return false;
    for (i = 0; i < n; ++i) {
        if (p[i] < 0x20U || p[i] == 0x7fU) return false;
        name[i] = (char)p[i];
    }
    name[n] = 0;
    return true;
}

/* The flag-byte LZSS of method 2: exactly @p out_size bytes, or failure. */
static bool cd_lzss(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    uint8_t *window = (uint8_t *)xx_mem_alloc(CD_WINDOW);
    size_t in_pos = 0U, out_pos = 0U, current = 1U;
    unsigned flag = 0U, bits = 0U;
    bool ok = false;

    if (!window) return false;
    xx_mem_zero(window, CD_WINDOW);
    while (out_pos < out_size) {
        if (bits == 0U) {
            if (in_pos >= in_size) goto done;
            flag = in[in_pos++];
            bits = 8U;
        }
        --bits;
        if (flag & 1U) {
            if (in_pos >= in_size) goto done;
            out[out_pos++] = window[current] = in[in_pos++];
            current = (current + 1U) & (CD_WINDOW - 1U);
        } else {
            size_t position, length, i;
            if (in_size - in_pos < 2U) goto done;
            position = ((size_t)(in[in_pos] & 0x0FU) << 8) | in[in_pos + 1U];
            length = (size_t)(in[in_pos] >> 4) + 2U;
            in_pos += 2U;
            for (i = 0U; i < length && out_pos < out_size; ++i) {
                uint8_t c = window[position];
                out[out_pos++] = window[current] = c;
                current = (current + 1U) & (CD_WINDOW - 1U);
                position = (position + 1U) & (CD_WINDOW - 1U);
            }
        }
        flag >>= 1;
    }
    ok = true;
done:
    xx_mem_free(window);
    return ok && out_pos == out_size;
}

/* Both methods are produced in memory, CRC-checked, then written. */
static bool cd_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    cd_member *ctx = (cd_member *)m->context;
    uint8_t *packed, *plain;
    size_t done = 0U;
    bool ok;

    if (!ctx || m->packed_size < 0 || m->packed_size > CD_MAX_MEMBER || m->size < 0 || m->size > CD_MAX_MEMBER) return false;
    packed = (uint8_t *)xx_mem_alloc(m->packed_size ? (size_t)m->packed_size : 1U);
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1U);
    if (!packed || !plain) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    ok = pm_read(f, m->offset - f->base_address, packed, (size_t)m->packed_size);
    if (ok) {
        if (m->compression_method == CD_METHOD_STORED) {
            ok = m->packed_size == m->size;
            if (ok && m->size) xx_rt_memcpy(plain, packed, (size_t)m->size);
        } else {
            ok = cd_lzss(packed, (size_t)m->packed_size, plain, (size_t)m->size);
        }
    }
    if (ok) ok = xx_crc32_calc(0, plain, (size_t)m->size) == ctx->crc && !(pd && xx_pd_is_stopped(pd));
    xx_mem_free(packed);
    while (ok && device && done < (size_t)m->size) {
        ssize_t w = xx_io_write(device, plain + done, (size_t)m->size - done);
        if (w <= 0 || (size_t)w > (size_t)m->size - done) ok = false;
        else done += (size_t)w;
    }
    xx_mem_free(plain);
    return ok;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t total = pm_available(f), at = 0;
    if (total < 3 || total > CD_TOTAL) return false;
    for (;;) {
        uint8_t head[2U + CD_NAME_MAX + 1U + CD_FIXED];
        size_t n, name_n;
        char name[96];
        uint32_t original, packed, crc;
        unsigned method;
        pm_member *m;
        cd_member *ctx;
        if ((pd && xx_pd_is_stopped(pd)) || at >= total) return false;
        n = total - at < (int64_t)sizeof(head) ? (size_t)(total - at) : sizeof(head);
        if (!pm_read(f, at, head, n)) return false;
        /* The terminator: a lone zero byte, and nothing after it. */
        if (!head[0]) return at + 1 == total && s->count > 0;
        if (n < 2U || head[0] != 0x03U || head[1] != 0x01U) return false;
        for (name_n = 0; 2U + name_n < n && head[2U + name_n]; ++name_n) {}
        if (2U + name_n >= n || 2U + name_n + 1U + CD_FIXED > n || !cd_name(head + 2, name_n, name)) return false;
        method = head[2U + name_n + 1U];
        original = cd_u32(head + 2U + name_n + 2U);
        packed = cd_u32(head + 2U + name_n + 6U);
        crc = cd_u32(head + 2U + name_n + 10U);
        if ((method != CD_METHOD_STORED && method != CD_METHOD_LZSS) || (method == CD_METHOD_STORED && original != packed) ||
            (uint64_t)packed > (uint64_t)(total - at) - (2U + name_n + 1U + CD_FIXED))
            return false;
        if (!pm_add(f, s, name, at + (int64_t)(2U + name_n + 1U + CD_FIXED), (int64_t)packed)) return false;
        m = &s->items[s->count - 1U];
        xx_rt_memcpy(m->name, name, name_n + 1U);
        m->size = original;
        m->compression_method = (uint16_t)method;
        ctx = (cd_member *)xx_mem_alloc(sizeof(*ctx));
        if (!ctx) return false;
        ctx->crc = crc;
        m->context = ctx;
        m->free_context = xx_mem_free;
        m->read_all = cd_read_all;
        at += (int64_t)(2U + name_n + 1U + CD_FIXED) + (int64_t)packed;
    }
}

Abstractformat *xx_cadc_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_CADC, "car");
    return f;
}

void xx_cadc_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}
