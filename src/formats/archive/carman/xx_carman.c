/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * CARMAN archives, the archiver of Mark Nelson's "The Data Compression Book".
 * Members follow each other without an archive header:
 *   char name[]  NUL-terminated (CARMAN keeps the name as given)
 *   u8   method  1 = stored, 2 = LZSS
 *   u32  original size, u32 packed size, u32 CRC-32 of the original data
 *   u32  CRC-32 of the name with its NUL and the 13 bytes before this field
 *   packed bytes
 * A lone NUL in place of a name ends the archive. CRC-32 is the reflected
 * 0xEDB88320 polynomial of ZIP.
 * Method 2 is NOT the book's bit-level LZSS: the archives in the wild use a
 * byte-oriented flag-byte LZSS (recovered 2026-11-08 from the samples and
 * verified by the stored CRC-32 on 327 of 327 members): a flag byte precedes
 * every eight tokens and is read from bit 0 up; a set bit is one literal
 * byte, a clear bit a two-byte match [b0, b1] with length (b0 >> 4) + 2 and
 * the ABSOLUTE position ((b0 & 15) << 8) | b1 in a 4096-byte ring that starts
 * zeroed with the write pointer at 1. Both member kinds are CRC-checked. */
#include "xxfclib/formats/archive/carman/xx_carman.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#define CM_NAME_MAX 95U
#define CM_HEADER 13U
#define CM_TOTAL INT64_C(1073741824)
#define CM_WINDOW 4096U
#define CM_MAX_MEMBER INT64_C(268435456)

/* The flag-byte LZSS of method 2: exactly @p out_size bytes, or failure. */
static bool cm_lzss(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    uint8_t *window = (uint8_t *)xx_mem_alloc(CM_WINDOW);
    size_t in_pos = 0U, out_pos = 0U, current = 1U;
    unsigned flag = 0U, bits = 0U;
    bool ok = false;

    if (!window) return false;
    xx_mem_zero(window, CM_WINDOW);
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
            current = (current + 1U) & (CM_WINDOW - 1U);
        } else {
            size_t position, length, i;
            if (in_size - in_pos < 2U) goto done;
            position = ((size_t)(in[in_pos] & 0x0FU) << 8) | in[in_pos + 1U];
            length = (size_t)(in[in_pos] >> 4) + 2U;
            in_pos += 2U;
            for (i = 0U; i < length && out_pos < out_size; ++i) {
                uint8_t c = window[position];
                out[out_pos++] = window[current] = c;
                current = (current + 1U) & (CM_WINDOW - 1U);
                position = (position + 1U) & (CM_WINDOW - 1U);
            }
        }
        flag >>= 1;
    }
    ok = true;
done:
    xx_mem_free(window);
    return ok && out_pos == out_size;
}

/* Method 2: inflate the whole member in memory, CRC-check, then write. */
static bool cm_read_lzss(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd, uint32_t expected_crc)
{
    uint8_t *packed, *plain;
    size_t done = 0U;
    bool ok;

    if (m->packed_size < 0 || m->packed_size > CM_MAX_MEMBER || m->size < 0 || m->size > CM_MAX_MEMBER) return false;
    packed = (uint8_t *)xx_mem_alloc(m->packed_size ? (size_t)m->packed_size : 1U);
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1U);
    if (!packed || !plain) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    ok = pm_read(f, m->offset - f->base_address, packed, (size_t)m->packed_size) &&
         cm_lzss(packed, (size_t)m->packed_size, plain, (size_t)m->size) &&
         xx_crc32_calc(0, plain, (size_t)m->size) == expected_crc && !(pd && xx_pd_is_stopped(pd));
    xx_mem_free(packed);
    while (ok && device && done < (size_t)m->size) {
        ssize_t w = xx_io_write(device, plain + done, (size_t)m->size - done);
        if (w <= 0 || (size_t)w > (size_t)m->size - done) ok = false;
        else done += (size_t)w;
    }
    xx_mem_free(plain);
    return ok;
}
typedef struct cm_member {
    uint32_t crc; /**< CRC-32 the original data must have */
} cm_member;
static uint32_t cm_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* Printable bytes only; CARMAN stores whatever path the shell handed it,
 * and the unpacker's own name policy decides what a slash may do. */
static bool cm_name(const uint8_t *p, size_t n, char name[96])
{
    size_t i;
    if (!n || n > CM_NAME_MAX) return false;
    for (i = 0; i < n; ++i) {
        if (p[i] < 0x20U || p[i] == 0x7fU) return false;
        name[i] = (char)p[i];
    }
    name[n] = 0;
    return true;
}
static bool cm_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    cm_member *ctx = (cm_member *)m->context;
    uint8_t buffer[16384];
    int64_t left = m->packed_size, at = m->offset - f->base_address;
    uint32_t crc = 0;
    if (!ctx) return false;
    if (m->compression_method == 2U) return cm_read_lzss(f, m, device, pd, ctx->crc);
    if (m->compression_method != 1U) return false;
    while (left > 0) {
        size_t n = left < (int64_t)sizeof(buffer) ? (size_t)left : sizeof(buffer), done = 0;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, at, buffer, n)) return false;
        crc = xx_crc32_calc(crc, buffer, n);
        while (device && done < n) {
            ssize_t w = xx_io_write(device, buffer + done, n - done);
            if (w <= 0 || (size_t)w > n - done) return false;
            done += (size_t)w;
        }
        at += (int64_t)n;
        left -= (int64_t)n;
    }
    return crc == ctx->crc && !(pd && xx_pd_is_stopped(pd));
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t total = pm_available(f), at = 0;
    if (total < 2 || total > CM_TOTAL) return false;
    for (;;) {
        uint8_t head[CM_NAME_MAX + 1U + 1U + CM_HEADER + 4U];
        size_t n, name_n;
        char name[96];
        uint32_t original, packed, crc, header_crc;
        unsigned method;
        pm_member *m;
        cm_member *ctx;
        if ((pd && xx_pd_is_stopped(pd)) || at >= total) return false;
        n = total - at < (int64_t)sizeof(head) ? (size_t)(total - at) : sizeof(head);
        if (!pm_read(f, at, head, n)) return false;
        /* The terminator: an empty name, and nothing after it. */
        if (!head[0]) return at + 1 == total && s->count > 0;
        for (name_n = 0; name_n < n && head[name_n]; ++name_n) {}
        if (name_n >= n || name_n + 1U + 1U + CM_HEADER + 4U > n || !cm_name(head, name_n, name)) return false;
        method = head[name_n + 1U];
        original = cm_u32(head + name_n + 2U);
        packed = cm_u32(head + name_n + 6U);
        crc = cm_u32(head + name_n + 10U);
        header_crc = cm_u32(head + name_n + 14U);
        if ((method != 1U && method != 2U) || (method == 1U && original != packed) ||
            (uint64_t)packed > (uint64_t)(total - at) - (name_n + 18U))
            return false;
        if (xx_crc32_calc(0, head, name_n + 1U + CM_HEADER) != header_crc) return false;
        if (!pm_add(f, s, name, at + (int64_t)name_n + 18, (int64_t)packed)) return false;
        m = &s->items[s->count - 1U];
        xx_rt_memcpy(m->name, name, name_n + 1U);
        m->size = original;
        m->compression_method = (uint16_t)method;
        ctx = (cm_member *)xx_mem_alloc(sizeof(*ctx));
        if (!ctx) return false;
        ctx->crc = crc;
        m->context = ctx;
        m->free_context = xx_mem_free;
        m->read_all = cm_read_all;
        at += (int64_t)name_n + 18 + (int64_t)packed;
    }
}
Abstractformat *xx_carman_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_CARMAN, "car");
    return f;
}
void xx_carman_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}
