/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield Express one-file self-extractor with a 16-bit (NE) stub.
 *
 * Carrier: "MZ" with e_lfanew (u32 at 0x3C) pointing at an "NE" header. The
 * payload is appended to the NE image and starts at the image end, the
 * largest of:
 *   - the segment extents: for each of the u16 at ne+0x1C entries of the
 *     segment table at ne + (u16 at ne+0x22), 8 bytes {u16 sector, u16
 *     length, u16 flags, u16 minimum allocation}; a non-zero sector gives
 *     (sector << shift) + (length ? length : 65536), where shift is the u16
 *     at ne+0x32 (0 means 9);
 *   - the resource extents: the resource table at ne + (u16 at ne+0x24), when
 *     it is not the resident-name table (ne + (u16 at ne+0x26)): u16 resource
 *     shift, then type blocks {u16 type id, u16 count, u32 reserved, count
 *     entries {u16 offset, u16 length, u16 flags, u16 id, u16 handle, u16
 *     usage}} up to a zero type id; an entry gives (offset + length) <<
 *     resource shift.
 *
 * Descriptor: the stub's resource of integer type 1024 (type id 0x8400)
 * starts with
 *   u32 0x190   descriptor size
 *   u32 5       descriptor generation
 *   u32         offset of the first record (the image end)
 *   u32         record count
 * This is the generation between the 0x110-byte generation 4 and the
 * 0x194-byte generation 6 descriptors of the InstallShield 3 self-extractors
 * (read by the installshield_3 reader); only generation 5 is accepted here.
 *
 * Payload: records from the image end to the end of the file, nothing else:
 *   u32  n       length of the source path, 1..260
 *   n    bytes   obfuscated path; byte i is ror8(c, 7 - (i & 7)) XOR
 *                KEY[i & 7], KEY = 95 6B D3 B5 49 9D 1F A0
 *   u32  stamp   DOS date in the low word, DOS time in the high word
 *   u32  size
 *   size bytes   the file, stored
 * The path is the full name of the file on the build machine
 * ("C:\...\DISK1\SETUP.EXE"); the member is its last component.
 *
 * The reader accepts a file only when the descriptor is present, its record
 * offset is the image end, its count is the number of records, and the whole
 * chain validates: every path is a plausible DOS/Windows path with a non-empty
 * last component, every size fits, and the last record ends exactly at the
 * end of the file. The format is known from one sample.
 */
#include "xxfclib/formats/archive/sfx_isexpress_ne/xx_sfx_isexpress_ne.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#define ISX_MAX_PATH 260U
#define ISX_MAX_MEMBERS 65535U
#define ISX_MAX_SEGMENTS 4096U
#define ISX_MAX_RESOURCES 65536U
#define ISX_RESOURCE_TYPE 0x8400U /* integer type 1024 */
static const uint8_t isx_magic[8] = {0x90U, 0x01U, 0x00U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U};
static const uint8_t isx_key[8] = {0x95U, 0x6BU, 0xD3U, 0xB5U, 0x49U, 0x9DU, 0x1FU, 0xA0U};
static uint32_t isx_u16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static uint32_t isx_u32(const uint8_t *p)
{
    return isx_u16(p) | (isx_u16(p + 2) << 16);
}
/* End of the NE image (segments and resources), or 0. Also requires the
 * generation 5 descriptor and returns its record offset and count. */
static int64_t isx_image_end(Abstractformat *f, int64_t total, uint32_t *data, uint32_t *records)
{
    uint8_t h[0x40], e[16];
    bool described = false;
    int64_t ne, table, names, end = 0, at;
    uint32_t count, shift, type, i, walked = 0;
    if (total < 0x80 || !pm_read(f, 0, h, 0x40) || h[0] != 'M' || h[1] != 'Z') return 0;
    ne = (int64_t)isx_u32(h + 0x3C);
    if (ne < 0x40 || ne > total - 0x40 || !pm_read(f, ne, h, 0x40) || h[0] != 'N' || h[1] != 'E') return 0;
    count = isx_u16(h + 0x1C);
    shift = isx_u16(h + 0x32);
    if (!shift) shift = 9;
    if (!count || count > ISX_MAX_SEGMENTS || shift > 15U) return 0;
    table = ne + (int64_t)isx_u16(h + 0x22);
    if (table > total || (int64_t)count * 8 > total - table) return 0;
    for (i = 0; i < count; ++i) {
        uint32_t sector, length;
        int64_t extent;
        if (!pm_read(f, table + (int64_t)i * 8, e, 8)) return 0;
        sector = isx_u16(e);
        length = isx_u16(e + 2);
        if (!sector) continue;
        extent = ((int64_t)sector << shift) + (length ? length : 65536U);
        if (extent > end) end = extent;
    }
    table = ne + (int64_t)isx_u16(h + 0x24);
    names = ne + (int64_t)isx_u16(h + 0x26);
    if (table != names) {
        if (!pm_read(f, table, e, 2)) return 0;
        shift = isx_u16(e);
        if (shift > 15U) return 0;
        at = table + 2;
        for (;;) {
            if (!pm_read(f, at, e, 2)) return 0;
            if (!isx_u16(e)) break;
            if (!pm_read(f, at, e, 8)) return 0;
            type = isx_u16(e);
            count = isx_u16(e + 2);
            at += 8;
            for (i = 0; i < count; ++i, at += 12) {
                int64_t extent;
                if (++walked > ISX_MAX_RESOURCES || !pm_read(f, at, e, 12)) return 0;
                extent = (int64_t)(isx_u16(e) + isx_u16(e + 2)) << shift;
                if (extent > end) end = extent;
                if (type == ISX_RESOURCE_TYPE && !described && ((int64_t)isx_u16(e + 2) << shift) >= 16) {
                    uint8_t d[16];
                    if (pm_read(f, (int64_t)isx_u16(e) << shift, d, 16) && !xx_rt_memcmp(d, isx_magic, sizeof(isx_magic))) {
                        *data = isx_u32(d + 8);
                        *records = isx_u32(d + 12);
                        described = true;
                    }
                }
            }
            if (++walked > ISX_MAX_RESOURCES) return 0;
        }
    }
    return described && end > ne + 0x40 && end < total ? end : 0;
}
/* Decodes a path in place and returns the offset of its last component, or
 * -1 when it is not a plausible path: bytes below 0x20, 0x7F or one of
 * < > " | ? *, a ':' anywhere but after a drive letter, an empty component,
 * or a last component that is empty, dots and spaces only, or longer than a
 * member name. */
static int isx_path(uint8_t *p, uint32_t n, size_t name_capacity)
{
    uint32_t i, leaf = 0;
    for (i = 0; i < n; ++i) {
        unsigned r = 7U - (i & 7U);
        uint8_t c = (uint8_t)(((p[i] >> r) | (p[i] << (8U - r))) & 0xFFU);
        c ^= isx_key[i & 7U];
        if (c < 0x20U || c == 0x7FU || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return -1;
        if (c == ':' && (i != 1U || !xx_archive_name_drive_letter((char)p[0]))) return -1;
        if (c == '/' || c == '\\') {
            /* "\\" may only open a UNC path. */
            if (i && i == leaf && !(i == 1U)) return -1;
            leaf = i + 1U;
        }
        p[i] = c;
    }
    if (n >= 2U && p[1] == ':' && leaf < 2U) leaf = 2U;
    if (leaf >= n || n - leaf >= name_capacity || xx_archive_name_dot_component((const char *)p + leaf, n - leaf)) return -1;
    return (int)leaf;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_legacy_archive_info *r = (xx_legacy_archive_info *)f;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, r->parse_options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    int64_t total = pm_available(f), at;
    uint8_t path[ISX_MAX_PATH + 12U];
    uint32_t data = 0, records = 0;
    r->incomplete = false;
    r->note = NULL;
    r->number_of_records = 0;
    at = isx_image_end(f, total, &data, &records);
    if (!at || (int64_t)data != at || !records || records > ISX_MAX_MEMBERS) return false;
    while (at < total) {
        uint32_t n, stamp, size, i;
        int leaf;
        pm_member *m;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (s->count >= records || total - at < 13 || !pm_read(f, at, path, 4)) return false;
        n = isx_u32(path);
        if (!n || n > ISX_MAX_PATH || (int64_t)n + 12 > total - at || !pm_read(f, at + 4, path, (size_t)n + 8U)) return false;
        stamp = isx_u32(path + n);
        size = isx_u32(path + n + 4U);
        at += (int64_t)n + 12;
        if ((int64_t)size > total - at) return false;
        leaf = isx_path(path, n, sizeof(s->items[0].name));
        if (leaf < 0) return false;
        if (limit && size > xx_var_get_u64(limit)) {
            if (pd) xx_pd_set_error(pd, 1, "member exceeds the size limit");
            return false;
        }
        if (!pm_add(f, s, "file", at, (int64_t)size)) return false;
        m = &s->items[s->count - 1U];
        for (i = 0; i < n - (uint32_t)leaf; ++i) {
            uint8_t c = path[(uint32_t)leaf + i];
            m->name[i] = (char)(c < 0x7FU && c != ':' ? c : '_');
        }
        m->name[i] = 0;
        while (i && (m->name[i - 1U] == '.' || m->name[i - 1U] == ' ')) m->name[--i] = '_';
        xx_archive_flat_name(m->name);
        m->compression_method = 0;
        /* No decoder is attached: the slot only carries the DOS stamp to the
         * record builder. */
        m->context = (void *)(uintptr_t)stamp;
        at += (int64_t)size;
    }
    if (at != total || s->count != records) return false;
    s->size = total;
    r->number_of_records = s->count;
    return true;
}
static const xx_archive_record *isx_current(Abstractformat *f, xx_archive_record_state *st)
{
    const xx_archive_record *record = ac_current(f, st);
    if (record) {
        const pm_stream *s = (const pm_stream *)st->internal_state;
        uint32_t stamp = (uint32_t)(uintptr_t)s->items[s->index].context;
        if (!(xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_LAST_MOD_DATE, stamp & 0xFFFFU) &&
              xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_LAST_MOD_TIME, (stamp >> 16) & 0xFFFFU)))
            return NULL;
    }
    return record;
}
void xx_sfx_isexpress_ne_init(xx_sfx_isexpress_ne *r, xx_io_device *d, int64_t base)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, base, XX_FILE_TYPE_SFX_ISEXPRESS_NE, "exe");
        r->format.create_archive_records_reading = ac_records;
        r->format.get_current_archive_record = isx_current;
    }
}
xx_sfx_isexpress_ne *xx_sfx_isexpress_ne_create(xx_io_device *d, int64_t base)
{
    xx_sfx_isexpress_ne *r = (xx_sfx_isexpress_ne *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_isexpress_ne_init(r, d, base);
    return r;
}
void xx_sfx_isexpress_ne_destroy(xx_sfx_isexpress_ne *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_isexpress_ne_free(xx_sfx_isexpress_ne *r)
{
    if (r) {
        xx_sfx_isexpress_ne_destroy(r);
        xx_mem_free(r);
    }
}
