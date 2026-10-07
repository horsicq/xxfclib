/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Bounded RIFF/ACON parser. Independently implemented using the format
 * description and writer at https://github.com/mxre/cursor/blob/master/anicursorgen.py
 * and the open-source reader at https://github.com/OxideAV/oxideav-ico .
 * The stored icon chunks are complete .cur/.ico files, so no image pixels
 * need to be rewritten to extract animation frames exactly.
 */
#include "xxfclib/formats/windows_ani/xx_windows_ani.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define ANI_MAX_FRAMES 4096U
#define ANI_MAX_STEPS 65536U
#define ANI_MAX_ICON_ENTRIES 65536U

static bool ani_id(const uint8_t *value, const char *id) {
    return xx_rt_memcmp(value, id, 4U) == 0;
}

/* Cursor/icon directory entries are kept intact; all offsets stay inside
 * their containing RIFF icon chunk. The decoded PNG/DIB pixels belong to the
 * existing ICO/CUR readers, not this frame extractor. */
static bool ani_icon(Abstractformat *f, int64_t at, uint32_t size,
                     bool *is_cursor, uint32_t *entry_budget,
                     xx_pd_struct *pd) {
    uint8_t header[6];
    uint16_t count;
    uint32_t i;
    if (size < 22U || !pm_read(f, at, header, sizeof(header)) ||
        xx_data_get_u16(header, 2, 0, false) != 0U ||
        (xx_data_get_u16(header + 2U, 2, 0, false) != 1U && xx_data_get_u16(header + 2U, 2, 0, false) != 2U))
        return false;
    *is_cursor = xx_data_get_u16(header + 2U, 2, 0, false) == 2U;
    count = xx_data_get_u16(header + 4U, 2, 0, false);
    if (!count || count > 4096U ||
        (uint64_t)6U + (uint64_t)count * 16U > size ||
        count > *entry_budget) return false;
    *entry_budget -= count;
    for (i = 0U; i < count; ++i) {
        uint8_t entry[16];
        uint32_t length, offset;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(f, at + 6 + (int64_t)i * 16, entry, sizeof(entry)))
            return false;
        length = xx_data_get_u32(entry + 8U, 4, 0, false);
        offset = xx_data_get_u32(entry + 12U, 4, 0, false);
        if (!length || offset < 6U + (uint32_t)count * 16U ||
            offset > size || length > size - offset) return false;
    }
    return true;
}

static bool ani_frames(Abstractformat *f, pm_stream *s, int64_t at,
                       int64_t end, uint32_t *entry_budget,
                       xx_pd_struct *pd) {
    while (at < end) {
        uint8_t chunk[8];
        uint32_t size;
        int64_t payload, next;
        bool cursor;
        char name[32];
        if ((pd && xx_pd_is_stopped(pd)) || end - at < 8 ||
            !pm_read(f, at, chunk, sizeof(chunk))) return false;
        size = xx_data_get_u32(chunk + 4U, 4, 0, false);
        payload = at + 8;
        if ((int64_t)size > end - payload ||
            (int64_t)(size & 1U) > end - payload - size) return false;
        next = payload + size + (size & 1U);
        if (ani_id(chunk, "icon")) {
            if (s->count >= ANI_MAX_FRAMES ||
                !ani_icon(f, payload, size, &cursor, entry_budget, pd))
                return false;
            (void)xx_rt_snprintf(name, sizeof(name), "frame-%04u.%s",
                                 (unsigned)s->count, cursor ? "cur" : "ico");
            if (!pm_add(f, s, name, payload, size)) return false;
        }
        at = next;
    }
    return at == end;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    uint8_t riff[12];
    uint32_t declared, frames = 0U, steps = 0U, flags = 0U;
    uint32_t entry_budget = ANI_MAX_ICON_ENTRIES;
    int64_t available = pm_available(f), end, at, sequence_at = -1;
    uint32_t sequence_size = 0U, rate_size = 0U;
    bool header_seen = false, fram_seen = false, seq_seen = false;
    bool rate_seen = false;
    if (available < 12 || !pm_read(f, 0, riff, sizeof(riff)) ||
        !ani_id(riff, "RIFF") || !ani_id(riff + 8U, "ACON")) return false;
    declared = xx_data_get_u32(riff + 4U, 4, 0, false);
    if (declared < 4U || (int64_t)declared > available - 8) return false;
    end = 8 + (int64_t)declared;
    for (at = 12; at < end;) {
        uint8_t chunk[8];
        uint32_t size;
        int64_t payload, next;
        if ((pd && xx_pd_is_stopped(pd)) || end - at < 8 ||
            !pm_read(f, at, chunk, sizeof(chunk))) return false;
        size = xx_data_get_u32(chunk + 4U, 4, 0, false);
        payload = at + 8;
        if ((int64_t)size > end - payload ||
            (int64_t)(size & 1U) > end - payload - size) return false;
        next = payload + size + (size & 1U);
        if (ani_id(chunk, "anih")) {
            uint8_t header[36];
            if (header_seen || fram_seen || size < sizeof(header) ||
                !pm_read(f, payload, header, sizeof(header)) ||
                xx_data_get_u32(header, 4, 0, false) != 36U) return false;
            frames = xx_data_get_u32(header + 4U, 4, 0, false);
            steps = xx_data_get_u32(header + 8U, 4, 0, false);
            flags = xx_data_get_u32(header + 32U, 4, 0, false);
            /* Some ANI writers leave cSteps zero to mean one identity step
             * for every frame. Resolve that convention before checking the
             * optional rate/sequence tables. */
            if (!steps) steps = frames;
            if (!frames || frames > ANI_MAX_FRAMES ||
                steps > ANI_MAX_STEPS || !(flags & 1U) || (flags & ~3U))
                return false;
            header_seen = true;
        } else if (ani_id(chunk, "LIST")) {
            uint8_t kind[4];
            if (size < 4U || !pm_read(f, payload, kind, sizeof(kind)))
                return false;
            if (ani_id(kind, "fram")) {
                if (!header_seen || fram_seen ||
                    !ani_frames(f, s, payload + 4, payload + size,
                                &entry_budget, pd))
                    return false;
                fram_seen = true;
            }
        } else if (ani_id(chunk, "seq ")) {
            if (!header_seen || seq_seen) return false;
            seq_seen = true;
            sequence_at = payload;
            sequence_size = size;
        } else if (ani_id(chunk, "rate")) {
            if (!header_seen || rate_seen) return false;
            rate_seen = true;
            rate_size = size;
        }
        at = next;
    }
    if (at != end || !header_seen || !fram_seen || s->count != frames ||
        (seq_seen && sequence_size != (uint64_t)steps * 4U) ||
        (rate_seen && rate_size != (uint64_t)steps * 4U) ||
        ((flags & 2U) != 0U && !seq_seen) ||
        (!seq_seen && steps != frames)) return false;
    if (seq_seen) {
        uint32_t i;
        for (i = 0U; i < steps; ++i) {
            uint8_t index[4];
            if ((pd && xx_pd_is_stopped(pd)) ||
                !pm_read(f, sequence_at + (int64_t)i * 4,
                         index, sizeof(index)) || xx_data_get_u32(index, 4, 0, false) >= frames)
                return false;
        }
    }
    s->size = end;
    return true;
}

void xx_windows_ani_init(xx_windows_ani *reader, xx_io_device *device,
                         int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address,
            XX_FILE_TYPE_WINDOWS_ANI, "ani");
    xx_format_set_mime_type(&reader->format, "application/x-navi-animation");
}

xx_windows_ani *xx_windows_ani_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_windows_ani *reader = (xx_windows_ani *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_windows_ani_init(reader, device, base_address);
    return reader;
}

void xx_windows_ani_destroy(xx_windows_ani *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_windows_ani_free(xx_windows_ani *reader) {
    if (!reader) return;
    xx_windows_ani_destroy(reader);
    xx_mem_free(reader);
}

bool xx_windows_ani_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    return pm_valid(self, pd);
}

bool xx_windows_ani_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    return pm_handle(self, pd);
}

int64_t xx_windows_ani_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
    return pm_size(self, pd);
}

uint64_t xx_windows_ani_get_number_of_archive_records(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    return pm_count(self, pd);
}

xx_archive_record_state *xx_windows_ani_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    return pm_create_records(self, options, pd);
}

const xx_archive_record *xx_windows_ani_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return pm_current(self, state);
}

bool xx_windows_ani_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    return pm_next(self, state, pd);
}

bool xx_windows_ani_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    return pm_unpack(self, state, pd);
}

void xx_windows_ani_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    pm_free_records(self, state);
}
