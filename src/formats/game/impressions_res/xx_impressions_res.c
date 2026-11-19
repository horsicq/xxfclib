/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * Impressions .RES resource archives (extension .gob; Lords of the Realm and
 * the other Impressions/Sierra titles of that engine). A header points to a
 * member table at the end of the file; every member is stored contiguously
 * from offset 8 and is either stored or packed with the engine's LZ codec.
 *
 *   0x00  4   char   ".RES"  (2E 52 45 53)
 *   0x04  u32 LE     dir_offset
 *   at dir_offset:
 *     u32 LE  count
 *     count * 52-byte entries:
 *       +0  36  char  name, NUL-terminated and NUL padded
 *       +36 u32 LE    flag (0 in every sample)
 *       +40 u32 LE    packed size
 *       +44 u32 LE    original size
 *       +48 u32 LE    offset (absolute, from the file start; data begins at 8)
 *   A member is stored when packed == original, else packed with the LZ codec
 *   (see ir_decompress). The directory is the file tail: its first entry's
 *   offset is 8 and dir_offset + 4 + count*52 == file size exactly, which is
 *   what makes ".RES" safely identifiable despite the short, common magic.
 */
#include "xxfclib/formats/game/impressions_res/xx_impressions_res.h"
#include "../../xx_payload_members.h"
#include "xxfclib/memory/xx_memory.h"

#define IR_ENTRY 52
#define IR_NAME 36U
#define IR_MAX_MEMBER INT64_C(268435456) /* 256 MiB ceiling per member */
#define IR_MAX_COUNT 4000000U

/* ---------------------------------------------------------------- codec -- */
/* The Impressions LZ codec. Reverse-engineered from the samples; produces
 * exactly out_size bytes or returns false. (Filled in from the lane RE.) */
static bool ir_decompress(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap);

/* ---------------------------------------------------------------- parse -- */
static uint32_t ir_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool ir_read_all(Abstractformat *f, pm_member *m, xx_io_device *device, xx_pd_struct *pd)
{
    uint8_t *packed, *plain;
    size_t done = 0U;
    bool ok;

    if (m->packed_size < 0 || m->packed_size > IR_MAX_MEMBER || m->size < 0 || m->size > IR_MAX_MEMBER) return false;
    packed = (uint8_t *)xx_mem_alloc(m->packed_size ? (size_t)m->packed_size : 1U);
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1U);
    if (!packed || !plain) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    ok = pm_read(f, m->offset - f->base_address, packed, (size_t)m->packed_size);
    if (ok) {
        if (m->compression_method == 0U) {
            ok = m->packed_size == m->size;
            if (ok && m->size) xx_rt_memcpy(plain, packed, (size_t)m->size);
        } else {
            ok = ir_decompress(packed, (size_t)m->packed_size, plain, (size_t)m->size);
        }
    }
    if (ok && (pd && xx_pd_is_stopped(pd))) ok = false;
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
    int64_t total = pm_available(f);
    uint8_t head[8];
    uint8_t *dir;
    uint32_t dir_off, count, i;
    int64_t dir_bytes;

    if (total < 12 || !pm_read(f, 0, head, 8)) return false;
    if (head[0] != 0x2EU || head[1] != 0x52U || head[2] != 0x45U || head[3] != 0x53U) return false; /* ".RES" */
    dir_off = ir_u32(head + 4);
    if (dir_off < 8U || (int64_t)dir_off + 4 > total) return false;
    {
        uint8_t cb[4];
        if (!pm_read(f, (int64_t)dir_off, cb, 4)) return false;
        count = ir_u32(cb);
    }
    if (count == 0U || count > IR_MAX_COUNT) return false;
    dir_bytes = (int64_t)count * IR_ENTRY;
    /* The directory is the exact file tail: this relation is the signature. */
    if ((int64_t)dir_off + 4 + dir_bytes != total) return false;

    dir = (uint8_t *)xx_mem_alloc((size_t)dir_bytes);
    if (!dir) return false;
    if (!pm_read(f, (int64_t)dir_off + 4, dir, (size_t)dir_bytes)) {
        xx_mem_free(dir);
        return false;
    }
    for (i = 0; i < count; ++i) {
        const uint8_t *e = dir + (size_t)i * IR_ENTRY;
        uint32_t packed = ir_u32(e + 40), original = ir_u32(e + 44), offset = ir_u32(e + 48);
        char name[IR_NAME + 1];
        size_t nl = 0;
        pm_member *m;

        while (nl < IR_NAME && e[nl]) {
            name[nl] = (e[nl] < 0x20U || e[nl] == 0x7FU) ? '_' : (char)e[nl];
            ++nl;
        }
        name[nl] = 0;
        if (!nl) { xx_rt_memcpy(name, "_unnamed", 9); nl = 8; }
        /* Every member lives inside the data region [8, dir_off). */
        if (offset < 8U || (int64_t)offset > total || (int64_t)packed > total - (int64_t)offset ||
            (int64_t)offset + (int64_t)packed > (int64_t)dir_off) {
            xx_mem_free(dir);
            return false;
        }
        if (!pm_add(f, s, name, (int64_t)offset, (int64_t)packed)) { xx_mem_free(dir); return false; }
        m = &s->items[s->count - 1];
        xx_rt_memcpy(m->name, name, nl + 1U);
        m->size = (int64_t)original;
        m->packed_size = (int64_t)packed;
        m->compression_method = (packed == original) ? 0U : 1U;
        m->read_all = ir_read_all;
        if (pd && xx_pd_is_stopped(pd)) { xx_mem_free(dir); return false; }
    }
    xx_mem_free(dir);
    return true;
}

Abstractformat *xx_impressions_res_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_IMPRESSIONS_RES, "gob");
    return f;
}

void xx_impressions_res_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

/* ============================ codec implementation ======================= */
/* PLACEHOLDER until the lane RE lands. Stored members extract correctly; a
 * packed member fails cleanly (no corruption) until this is replaced by the
 * reverse-engineered decoder from F:\ARC\re\gob_res\decomp.c. */
#include "xx_impressions_res_codec.inc"
