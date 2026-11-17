/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Consultant Plus database containers (.ANS answer databases and raw chunk
 * dumps). The file is a sequence of little-endian TLV chunks:
 *
 *   +0 u16 tag, +2 u32 total size including this 8-byte header, +6 u16 extra
 *
 * Single 0xFF bytes between chunks are padding. Two file shapes exist:
 *   envelope: a first chunk tag 0x00D2 of size 0xD2, extra != 0, and the bytes
 *             00 00 0D 0A at 0x26;
 *   raw:      a first chunk tag 0x819A whose size is the whole file, holding a
 *             first sub-chunk of size 8 .. file size - 8.
 * Container tags: 0x819A nests the same chunk grammar; 0x819B is a stored file
 * (0x019C name, 0x019D data); 0x80EA is a document block. Every other tag is
 * skipped by its size.
 *
 * A document block holds 0x80EC descriptor chunks (only before the first
 * 0x00F1) with slice tables 0x016C..0x016F: entries of one type byte and a
 * 1..4 byte little-endian slice size. The 0x00F1 chunks concatenate into one
 * block buffer, each either a zlib stream (78 9C) or raw bytes. When the slice
 * sizes add up to the buffer exactly, the slices are walked in order:
 *   type 0x12 text: 0x0175 chunks are zlib streams forming the text; 0x0176
 *        holds 6-byte line records. The text is mapped 0x00 -> LF and
 *        0x01..0x1F -> space and published as "NNNNN.txt" (running number);
 *   type 0x5D/0x5E object: 0x017D chunks with 0x017E name (Windows-1251),
 *        0x017F stored or 0x0180 zlib data and 0x018A u32 picture format
 *        (0/1 bmp, 2 png, 3 jpg, else bin); published as "NNNNN.<name>" where
 *        NNNNN is the text counter at the start of the block, or
 *        "NNNNN.MMMMM.<ext>" for unnamed objects.
 * There are no container checksums; the zlib streams carry their own.
 */
#include "xxfclib/formats/archive/consplus/xx_consplus.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#define CP_DEPTH 16U
typedef struct cp_state {
    Abstractformat *f;
    pm_stream *s;
    ac_blob *b;
    uint32_t texts;
} cp_state;
static uint32_t cp_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static uint16_t cp_u16(const uint8_t *p)
{
    return xx_data_get_u16(p, 2, 0, false);
}
/* A zlib stream of unknown plaintext size: grow the buffer until the stream
 * fits, bounded by the archive memory budget. */
static bool cp_inflate(ac_blob *b, const uint8_t *in, uint32_t n, uint8_t **out, uint32_t *produced)
{
    uint32_t cap = n < 4096U ? 16384U : (n > AC_MAX_BYTES / 8U ? AC_MAX_BYTES : n * 8U);
    *out = NULL;
    *produced = 0;
    if (n < 2U || !xx_zlib_stream_header_is_valid(in, n)) return false;
    for (;;) {
        size_t written = 0;
        uint8_t *p;
        if (!ac_poll(b)) return false;
        p = ac_alloc(b, cap);
        if (!p) return false;
        if (xx_zlib_stream_decode_memory(in, n, p, cap, &written) && written < cap) {
            if (!ac_compact(b, &p, cap, (uint32_t)written)) {
                ac_release(b, p, cap);
                return false;
            }
            *out = p;
            *produced = (uint32_t)written;
            return true;
        }
        ac_release(b, p, cap);
        if (cap >= AC_MAX_BYTES) return false;
        cap = cap > AC_MAX_BYTES / 2U ? AC_MAX_BYTES : cap * 2U;
    }
}
/* Append src to a growing buffer owned by the archive budget. */
static bool cp_append(ac_blob *b, uint8_t **buf, uint32_t *n, uint32_t *cap, const uint8_t *src, uint32_t add)
{
    if (add > AC_MAX_BYTES - *n) return false;
    if (*n + add > *cap) {
        uint32_t next = *cap ? *cap : 4096U;
        uint8_t *p;
        while (next < *n + add) next = next > AC_MAX_BYTES / 2U ? AC_MAX_BYTES : next * 2U;
        p = ac_alloc(b, next);
        if (!p) return false;
        if (*n) xx_rt_memcpy(p, *buf, *n);
        ac_release(b, *buf, *cap);
        *buf = p;
        *cap = next;
    }
    if (add) xx_rt_memcpy(*buf + *n, src, add);
    *n += add;
    return true;
}
/* Windows-1251 to UTF-8 for member names; control and unmapped bytes become '_'. */
static void cp_name(char *out, size_t cap, const char *prefix, const uint8_t *p, uint32_t n)
{
    size_t at = 0, i;
    for (i = 0; prefix[i] && at + 1U < cap; ++i) out[at++] = prefix[i];
    for (i = 0; i < n && p[i] && at + 3U < cap; ++i) {
        uint8_t c = p[i];
        uint32_t u = c;
        if (c >= 0xC0U) u = 0x410U + (c - 0xC0U);
        else if (c == 0xA8U) u = 0x401U;
        else if (c == 0xB8U) u = 0x451U;
        else if (c < 0x20U || c >= 0x7FU || c == '/' || c == '\\' || c == ':') u = '_';
        if (u < 0x80U) out[at++] = (char)u;
        else {
            out[at++] = (char)(0xC0U | (u >> 6));
            out[at++] = (char)(0x80U | (u & 0x3FU));
        }
    }
    out[at] = 0;
}
static bool cp_emit(cp_state *st, char *display, uint8_t *data, uint32_t n)
{
    pm_member *m;
    /* Names are flat: never "", "." or ".." (cp_name maps separators). */
    xx_archive_flat_name(display);
    if (!ac_memory(st->f, st->s, st->b, "member", data, n, n, 0)) return false;
    m = &st->s->items[st->s->count - 1U];
    m->display_name = xx_str_dup(display);
    if (!m->display_name) return false;
    xx_rt_snprintf(m->name, sizeof(m->name), "%s", display);
    return true;
}
/* Walk the sub-chunks of [p, p + n): calls back nothing, just validates one step. */
static bool cp_chunk(const uint8_t *p, uint32_t left, uint16_t *tag, uint32_t *size)
{
    if (left < 8U) return false;
    *tag = cp_u16(p);
    *size = cp_u32(p + 2);
    return *size >= 8U && *size <= left;
}
static bool cp_text(cp_state *st, const uint8_t *p, uint32_t n)
{
    uint8_t *text = NULL;
    uint32_t len = 0, cap = 0, i;
    char name[32];
    /* The running number is taken even when the slice turns out malformed. */
    xx_rt_snprintf(name, sizeof(name), "%05u.txt", (unsigned)st->texts++);
    while (n) {
        uint16_t tag;
        uint32_t size;
        if (!cp_chunk(p, n, &tag, &size)) {
            ac_release(st->b, text, cap);
            return true; /* a malformed text slice is skipped, as the writer's reader does */
        }
        if (tag == 0x0176U && (size - 8U) % 6U) {
            ac_release(st->b, text, cap);
            return true;
        }
        if (tag == 0x0175U) {
            uint8_t *part;
            uint32_t got;
            bool ok;
            if (!cp_inflate(st->b, p + 8, size - 8U, &part, &got)) {
                ac_release(st->b, text, cap);
                return false;
            }
            ok = cp_append(st->b, &text, &len, &cap, part, got);
            ac_release(st->b, part, got);
            if (!ok) {
                ac_release(st->b, text, cap);
                return false;
            }
        }
        p += size;
        n -= size;
    }
    for (i = 0; i < len; ++i) {
        if (!text[i]) text[i] = 0x0A;
        else if (text[i] < 0x20U) text[i] = 0x20;
    }
    if (!text) {
        text = ac_alloc(st->b, 0);
        cap = 0;
        if (!text) return false;
    } else if (!ac_compact(st->b, &text, cap, len)) {
        ac_release(st->b, text, cap);
        return false;
    }
    return cp_emit(st, name, text, len);
}
static bool cp_object(cp_state *st, const uint8_t *p, uint32_t n, uint32_t parent, uint32_t *images)
{
    while (n) {
        uint16_t tag;
        uint32_t size;
        if (!cp_chunk(p, n, &tag, &size)) return true;
        if (tag == 0x017DU) {
            const uint8_t *q = p + 8, *name = NULL, *data = NULL;
            uint32_t left = size - 8U, name_n = 0, data_n = 0, format = 0;
            bool packed = false;
            while (left) {
                uint16_t t;
                uint32_t z;
                if (!cp_chunk(q, left, &t, &z)) break;
                if (t == 0x017EU) {
                    name = q + 8;
                    name_n = z - 8U;
                } else if (t == 0x017FU || t == 0x0180U) {
                    data = q + 8;
                    data_n = z - 8U;
                    packed = t == 0x0180U;
                } else if (t == 0x018AU && z == 12U) format = cp_u32(q + 8);
                q += z;
                left -= z;
            }
            if (data) {
                char label[300], prefix[16];
                uint8_t *out;
                uint32_t out_n;
                if (packed) {
                    if (!cp_inflate(st->b, data, data_n, &out, &out_n)) return false;
                } else {
                    out = ac_alloc(st->b, data_n);
                    if (!out) return false;
                    if (data_n) xx_rt_memcpy(out, data, data_n);
                    out_n = data_n;
                }
                while (name_n && !name[name_n - 1U]) --name_n;
                if (name_n) {
                    xx_rt_snprintf(prefix, sizeof(prefix), "%05u.", (unsigned)parent);
                    cp_name(label, sizeof(label), prefix, name, name_n);
                } else {
                    static const char *const ext[] = {"bmp", "bmp", "png", "jpg"};
                    xx_rt_snprintf(label, sizeof(label), "%05u.%05u.%s", (unsigned)parent, (unsigned)(*images)++, format < 4U ? ext[format] : "bin");
                }
                if (!cp_emit(st, label, out, out_n)) return false;
            }
        }
        p += size;
        n -= size;
    }
    return true;
}
static bool cp_document(cp_state *st, const uint8_t *p, uint32_t n)
{
    uint8_t *types = NULL, *block = NULL;
    uint32_t *sizes = NULL, slices = 0, slice_cap = 0, block_n = 0, block_cap = 0, parent = st->texts, images = 0, i;
    uint64_t total = 0;
    bool saw_data = false, ok = true;
    while (n && ok) {
        uint16_t tag;
        uint32_t size;
        if (!cp_chunk(p, n, &tag, &size)) break;
        if (tag == 0x80ECU) {
            const uint8_t *q = p + 8;
            uint32_t left = size - 8U;
            if (saw_data) break;
            while (left) {
                uint16_t t;
                uint32_t z;
                if (!cp_chunk(q, left, &t, &z)) break;
                if (t >= 0x016CU && t <= 0x016FU) {
                    uint32_t width = (uint32_t)(t - 0x016CU) + 1U, entry = width + 1U, count = (z - 8U) / entry, k;
                    if ((z - 8U) % entry == 0U && count) {
                        if (count > 1000000U - slices) {
                            ok = false;
                            break;
                        }
                        if (slices + count > slice_cap) {
                            uint32_t next = slice_cap ? slice_cap : 64U;
                            uint8_t *nt;
                            uint32_t *ns;
                            while (next < slices + count) next *= 2U;
                            nt = ac_alloc(st->b, next);
                            ns = (uint32_t *)ac_alloc(st->b, next * 4U);
                            if (!nt || !ns) {
                                ac_release(st->b, nt, next);
                                ac_release(st->b, (uint8_t *)ns, next * 4U);
                                ok = false;
                                break;
                            }
                            if (slices) {
                                xx_rt_memcpy(nt, types, slices);
                                xx_rt_memcpy(ns, sizes, slices * 4U);
                            }
                            ac_release(st->b, types, slice_cap);
                            ac_release(st->b, (uint8_t *)sizes, slice_cap * 4U);
                            types = nt;
                            sizes = ns;
                            slice_cap = next;
                        }
                        for (k = 0; k < count; ++k) {
                            const uint8_t *e = q + 8 + k * entry;
                            uint32_t v = 0, j;
                            for (j = 0; j < width; ++j) v |= (uint32_t)e[1 + j] << (8U * j);
                            types[slices] = e[0];
                            sizes[slices++] = v;
                            total += v;
                        }
                    }
                }
                q += z;
                left -= z;
            }
        } else if (tag == 0x00F1U) {
            saw_data = true;
            if (size - 8U >= 2U && p[8] == 0x78U && p[9] == 0x9CU) {
                uint8_t *part;
                uint32_t got;
                if (!cp_inflate(st->b, p + 8, size - 8U, &part, &got)) ok = false;
                else {
                    ok = cp_append(st->b, &block, &block_n, &block_cap, part, got);
                    ac_release(st->b, part, got);
                }
            } else ok = cp_append(st->b, &block, &block_n, &block_cap, p + 8, size - 8U);
        }
        p += size;
        n -= size;
    }
    if (ok && total == block_n) {
        uint32_t at = 0;
        for (i = 0; i < slices && ok; ++i) {
            if (types[i] == 0x12U) ok = cp_text(st, block + at, sizes[i]);
            else if (types[i] == 0x5DU || types[i] == 0x5EU) ok = cp_object(st, block + at, sizes[i], parent, &images);
            at += sizes[i];
        }
    }
    ac_release(st->b, types, slice_cap);
    ac_release(st->b, (uint8_t *)sizes, slice_cap * 4U);
    ac_release(st->b, block, block_cap);
    return ok;
}
static bool cp_file(cp_state *st, const uint8_t *p, uint32_t n)
{
    const uint8_t *name = NULL, *data = NULL;
    uint32_t name_n = 0, data_n = 0;
    while (n) {
        uint16_t tag;
        uint32_t size;
        if (!cp_chunk(p, n, &tag, &size)) break;
        if (tag == 0x019CU) {
            name = p + 8;
            name_n = size - 8U;
        } else if (tag == 0x019DU) {
            data = p + 8;
            data_n = size - 8U;
        }
        p += size;
        n -= size;
    }
    while (name_n && !name[name_n - 1U]) --name_n;
    if (name_n && data) {
        char label[300];
        uint8_t *out = ac_alloc(st->b, data_n);
        if (!out) return false;
        if (data_n) xx_rt_memcpy(out, data, data_n);
        cp_name(label, sizeof(label), "", name, name_n);
        return cp_emit(st, label, out, data_n);
    }
    return true;
}
static bool cp_chunks(cp_state *st, uint32_t at, uint32_t end, unsigned depth)
{
    if (depth > CP_DEPTH) return false;
    while (at < end) {
        uint16_t tag;
        uint32_t size;
        if (!ac_poll(st->b)) return false;
        if (st->b->p[at] == 0xFFU) {
            ++at;
            continue;
        }
        if (!cp_chunk(st->b->p + at, end - at, &tag, &size)) break;
        if (tag == 0x80EAU) {
            if (!cp_document(st, st->b->p + at + 8, size - 8U)) return false;
        } else if (tag == 0x819AU) {
            if (!cp_chunks(st, at + 8U, at + size, depth + 1U)) return false;
        } else if (tag == 0x819BU) {
            if (!cp_file(st, st->b->p + at + 8, size - 8U)) return false;
        }
        at += size;
    }
    return true;
}
static bool cp_detect(const uint8_t *p, uint32_t n)
{
    if (n >= 42U && cp_u16(p) == 0x00D2U && cp_u32(p + 2) == 0xD2U && cp_u16(p + 6) && !p[0x26] && !p[0x27] && p[0x28] == 0x0DU && p[0x29] == 0x0AU) return true;
    if (n >= 14U && cp_u16(p) == 0x819AU && cp_u32(p + 2) == n) {
        uint32_t sub = cp_u32(p + 10);
        return sub > 7U && sub <= n - 8U;
    }
    return false;
}
static bool cp_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    cp_state st;
    if (!cp_detect(b->p, b->n)) return false;
    st.f = f;
    st.s = s;
    st.b = b;
    st.texts = 0;
    return cp_chunks(&st, 0, b->n, 0) && s->count;
}
AC_PARSE(cp_parse)
AC_DEFINE(consplus, XX_FILE_TYPE_CONSPLUS_DB, "ans")
