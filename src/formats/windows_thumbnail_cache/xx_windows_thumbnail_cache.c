/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Format facts: libyal/libwtcdb documentation; thumbcacheviewer database layouts.
 * The reflected CRC polynomial is inferred from the Windows CRC table (entry128).
 */
#include "xxfclib/formats/windows_thumbnail_cache/xx_windows_thumbnail_cache.h"
#include "../ue2_indexed.h"
#ifdef WINDOWS_THUMBNAIL_CACHE
#define UE2_THUMBCACHE_TYPE XX_FILE_TYPE_WINDOWS_THUMBNAIL_CACHE
#else
#define UE2_THUMBCACHE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static uint64_t thumb_crc(uint64_t crc, const uint8_t *data, size_t size) {
    size_t i; unsigned bit;
    for (i = 0; i < size; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? UINT64_C(0x92c64265d32139a4) : 0);
    }
    return crc;
}
static ue2_index *thumb_parse(Abstractformat *f, uint32_t *version_out, uint32_t *type_out, xx_pd_struct *pd) {
    uint8_t header[28], entry[56], magic[8];
    uint32_t version, type, header_size = 24, fields, entry_size, first, available;
    int64_t total = f && f->device ? xx_io_total_size(f->device) : -1, end, cursor;
    ue2_index *index = NULL;
    if (!f || f->base_address < 0 || !ue2_read(f, f->base_address, header, 24) || xx_rt_memcmp(header, "CMMM", 4)) return NULL;
    version = ue2_u32(header + 4); type = ue2_u32(header + 8);
    if (version == 20 || version == 21) {
        first = ue2_u32(header + 12); available = ue2_u32(header + 16);
        if (type > 4) return NULL;
    } else if (version == 26) {
        first = ue2_u32(header + 12); available = ue2_u32(header + 16);
        if (type > 8) return NULL;
    } else if (version == 28 || version == 30 || version == 31 || version == 32) {
        if (version == 28) { header_size = 28; if (!ue2_read(f, f->base_address, header, 28)) return NULL; }
        first = ue2_u32(header + 16); available = ue2_u32(header + 20);
        if (type > (version == 31 ? 10U : version == 32 ? 13U : 8U)) return NULL;
    } else return NULL;
    end = total - f->base_address;
    if (available) { if (available < header_size || available > end) return NULL; end = available; }
    if (!first) first = header_size;
    if (first < header_size || first > end) return NULL;
    index = (ue2_index *)xx_mem_calloc(1, sizeof(*index));
    if (!index) return NULL;
    fields = version == 20 ? 24 : 16; entry_size = version == 21 ? 48 : 56;
    cursor = first;
    while (cursor < end) {
        uint32_t length, identifier, padding, size;
        int64_t data;
        uint64_t hash;
        char name[80]; const char *extension = "bin";
        if ((pd && xx_pd_is_stopped(pd)) || !ue2_range(end, cursor, entry_size) ||
            !ue2_read(f, f->base_address + cursor, entry, entry_size) || xx_rt_memcmp(entry, "CMMM", 4)) goto fail;
        length = ue2_u32(entry + 4); hash = ue2_u64(entry + 8);
        identifier = ue2_u32(entry + fields); padding = ue2_u32(entry + fields + 4); size = ue2_u32(entry + fields + 8);
        if (length < entry_size || !ue2_range(end, cursor, length) || identifier & 1U ||
            identifier > 1024U * 1024U || (uint64_t)entry_size + identifier + padding + size > length) goto fail;
        /* An unused terminal entry can reserve all remaining physical space. */
        if (!identifier && !padding && !size && !hash) { cursor += length; continue; }
        if (thumb_crc(UINT64_MAX, entry, entry_size - 8) != ue2_u64(entry + entry_size - 8)) goto fail;
        if (size) {
            data = f->base_address + cursor + entry_size + identifier + padding;
            if (!ue2_read(f, data, magic, size < 8 ? size : 8)) goto fail;
            if (size >= 8 && !xx_rt_memcmp(magic, "\x89PNG\r\n\x1a\n", 8)) extension = "png";
            else if (size >= 3 && magic[0] == 0xff && magic[1] == 0xd8 && magic[2] == 0xff) extension = "jpg";
            else if (size >= 2 && magic[0] == 'B' && magic[1] == 'M') extension = "bmp";
            xx_rt_snprintf(name, sizeof(name), "%016llx_%08llx.%s", (unsigned long long)hash, (unsigned long long)cursor, extension);
            if (!ue2_add(index, name, data, size, ue2_u64(entry + entry_size - 16))) goto fail;
        }
        cursor += length;
    }
    index->size = total - f->base_address;
    if (version_out) *version_out = version;
    if (type_out) *type_out = type;
    return index;
fail: ue2_index_free(index); return NULL;
}
static bool thumb_valid(Abstractformat *f, xx_pd_struct *pd) {
    ue2_index *index = thumb_parse(f, NULL, NULL, pd);
    bool valid = index != NULL; ue2_index_free(index); return valid;
}
static bool thumb_info(Abstractformat *f, xx_pd_struct *pd) {
    xx_windows_thumbnail_cache *a = (xx_windows_thumbnail_cache *)f;
    return ue2_accept(f, thumb_parse(f, &a->version, &a->cache_type, pd));
}
static bool thumb_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd) {
    const ue2_member *m;
    ue2_state *s;
    uint8_t *buffer;
    uint64_t first = 0, second = 0;
    int64_t done = 0;
    bool valid = false;
    int level;
    if (!ue2_current(f, state) || !state->internal_state) return false;
    s = (ue2_state *)state->internal_state; m = &s->index->members[s->cursor];
    buffer = (uint8_t *)xx_mem_alloc(65536);
    if (!buffer) return false;
    level = xx_pd_enter_level(pd, (uint64_t)m->size, "Verifying thumbnail checksum");
    while (done < m->size) {
        size_t take = m->size - done > 65536 ? 65536U : (size_t)(m->size - done);
        if ((pd && xx_pd_is_stopped(pd)) || !ue2_read(f, m->offset + done, buffer, take)) goto cleanup;
        if (!done) {
            size_t initial = take < 1024 ? take : 1024;
            first = thumb_crc(first, buffer, initial);
        }
        if (done + (int64_t)take > 1024) {
            int64_t sample = done > 1024 ? 1024 + ((done - 1024) / 400) * 400 : 1024;
            while (sample < done + (int64_t)take) {
                int64_t low = sample < done ? done : sample;
                int64_t high = sample + 4 > done + (int64_t)take ? done + (int64_t)take : sample + 4;
                if (high > low) second = thumb_crc(second, buffer + (size_t)(low - done), (size_t)(high - low));
                sample += 400;
            }
        }
        done += take; xx_pd_set_current(pd, level, (uint64_t)done);
    }
    valid = !(pd && xx_pd_is_stopped(pd)) && (first ^ second) == m->tag;
cleanup:
    xx_pd_leave_level(pd, level); xx_mem_free(buffer);
    return valid && ue2_unpack(f, state, pd);
}
void xx_windows_thumbnail_cache_init(xx_windows_thumbnail_cache *a, xx_io_device *device, int64_t base) {
    if (!a) { return; } xx_mem_zero(a, sizeof(*a));
    ue2_init_format(&a->format, device, base, UE2_THUMBCACHE_TYPE, "db", "application/x-windows-thumbnail-cache");
    a->format.check_is_valid = thumb_valid; a->format.handle_base_info = thumb_info;
    a->format.unpack_current_archive_record = thumb_unpack;
}
xx_windows_thumbnail_cache *xx_windows_thumbnail_cache_create(xx_io_device *device, int64_t base) {
    xx_windows_thumbnail_cache *a = (xx_windows_thumbnail_cache *)xx_mem_alloc(sizeof(*a));
    if (a) { xx_windows_thumbnail_cache_init(a, device, base); } return a;
}
void xx_windows_thumbnail_cache_destroy(xx_windows_thumbnail_cache *a) { if (a) ue2_destroy_format(&a->format); }
void xx_windows_thumbnail_cache_free(xx_windows_thumbnail_cache *a) { if (a) { xx_windows_thumbnail_cache_destroy(a); xx_mem_free(a); } }
