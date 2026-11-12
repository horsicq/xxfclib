/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * MAS resource pack: the flat stored container of a DOS game seen as
 * E3T2.MAS .. E3T4.MAS with *.VRL members. Layout derived from those files:
 *   u32 count; u32 data_size;
 *   count x { char name[16]; u32 offset; u32 size; u32 packed; u32 zero }
 *   data_size bytes of member data, offsets relative to the end of the
 *   table, the members back to back in table order; `packed` is zero in a
 *   stored pack and the packed byte count in the packed one (zlib behind a
 *   16-byte image sub-header).
 * The container has no magic. Validity is the table arithmetic (the table
 * plus data_size equal the file, every member starts where the previous one
 * ended) and well-formed names. */
#include "xxfclib/formats/game/mas_pack/xx_mas_pack.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/memory/xx_memory.h"
#define MP_MAX_COUNT 65536U
#define MP_TOTAL INT64_C(2147483648)
static uint32_t mp_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/* 1..16 printable bytes without path or wildcard characters, zero padded. */
static bool mp_name(const uint8_t *p, char name[17])
{
    size_t n = 0, i;
    while (n < 16U && p[n]) ++n;
    if (!n) return false;
    for (i = n; i < 16U; ++i)
        if (p[i]) return false;
    for (i = 0; i < n; ++i) {
        unsigned c = p[i];
        if (c < 0x21U || c > 0x7eU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
        name[i] = (char)c;
    }
    name[n] = 0;
    return true;
}
/* Packed members (recovered 2026-11-08, verified on 579 of 579 entries): the
 * payload opens with a 16-byte image sub-header {u32 width; u32 height;
 * u32 method; u32 flags}; method 1 is a zlib (RFC 1950) stream that
 * inflates to exactly `size` (= width * height * 3) bytes of 24-bit BGR
 * pixels, method 0 is stored (the pixels follow the sub-header when the
 * sizes say so, else the entry is taken verbatim). */
#define MP_SUBHEADER 16U
#define MP_MAX_MEMBER INT64_C(268435456)
static bool mp_read_packed(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    uint8_t *packed, *plain;
    size_t done = 0U, written = 0U, out = (size_t)m->size;
    uint32_t method, width, height;
    bool ok = false;

    if (m->packed_size < (int64_t)MP_SUBHEADER || m->packed_size > MP_MAX_MEMBER || m->size < 0 || m->size > MP_MAX_MEMBER) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)m->packed_size);
    plain = (uint8_t *)xx_mem_alloc(out ? out : 1U);
    if (!packed || !plain || !pm_read(f, m->offset - f->base_address, packed, (size_t)m->packed_size)) goto done;
    width = mp_u32(packed);
    height = mp_u32(packed + 4);
    method = mp_u32(packed + 8);
    if (method == 1U) {
        ok = xx_zlib_stream_decode_memory(packed + MP_SUBHEADER, (size_t)m->packed_size - MP_SUBHEADER, plain, out, &written) && written == out;
    } else if (method == 0U) {
        if ((uint64_t)width * height * 3U == (uint64_t)out && (size_t)m->packed_size - MP_SUBHEADER >= out) {
            xx_rt_memcpy(plain, packed + MP_SUBHEADER, out);
            ok = true;
        } else if ((size_t)m->packed_size >= out) {
            xx_rt_memcpy(plain, packed, out);
            ok = true;
        } else if (m->offset - f->base_address + m->size <= pm_available(f)) {
            /* A stored entry whose packed field undercounts (AMBC.BMP: 779 for
             * the 784 bytes that really sit before the next member): the
             * directory's size is the truth. */
            ok = pm_read(f, m->offset - f->base_address, plain, out);
        }
    }
    if (ok && pd && xx_pd_is_stopped(pd)) ok = false;
    while (ok && device && done < out) {
        ssize_t w = xx_io_write(device, plain + done, out - done);
        if (w <= 0 || (size_t)w > out - done) ok = false;
        else done += (size_t)w;
    }
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32];
    int64_t total = pm_available(f), table;
    uint32_t count, data_size, i;
    uint64_t expect = 0;
    if (total < 8 + 32 || total > MP_TOTAL || !pm_read(f, 0, h, 8)) return false;
    count = mp_u32(h);
    data_size = mp_u32(h + 4);
    if (!count || count > MP_MAX_COUNT || (uint64_t)count * 32U > (uint64_t)(total - 8)) return false;
    table = 8 + (int64_t)count * 32;
    if ((uint64_t)data_size != (uint64_t)(total - table)) return false;
    for (i = 0; i < count; ++i) {
        char name[17];
        uint32_t offset, size, packed;
        pm_member *m;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 8 + (int64_t)i * 32, h, sizeof(h)) || !mp_name(h, name)) return false;
        offset = mp_u32(h + 16);
        size = mp_u32(h + 20);
        packed = mp_u32(h + 24);
        /* The third word is zero in a stored pack; in a packed one (gizmo.mas,
         * LOADING.MAS: about 3:1) it is the member's packed size and the
         * next member starts after it; mp_read_packed inflates those. */
        /* Members follow each other; a few packs leave a few bytes of slack
         * between two of them, so only an overlap is a fault. */
        if (mp_u32(h + 28) || offset < expect || offset > data_size || (packed ? packed : size) > data_size - offset) return false;
        expect = (uint64_t)offset + (packed ? packed : size);
        if (!pm_add(f, s, name, table + offset, packed ? packed : size)) return false;
        m = &s->items[s->count - 1U];
        xx_rt_memcpy(m->name, name, xx_rt_strlen(name) + 1U);
        if (packed) {
            m->size = size;
            m->compression_method = 1U;
            m->read_all = mp_read_packed;
        }
    }
    if (expect > data_size) return false;
    s->size = total;
    return !(pd && xx_pd_is_stopped(pd));
}
Abstractformat *xx_mas_pack_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_MAS_PACK, "mas");
    return f;
}
void xx_mas_pack_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}
