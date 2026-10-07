/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original bounded native reader for PCE PFI version-0 flux streams.
 * PCE is used only to produce and verify test fixtures.
 */
#include "xxfclib/formats/pce_pfi/xx_pce_pfi.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include <stdlib.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef PCE_PFI
#define PFI_FILE_TYPE XX_FILE_TYPE_PCE_PFI
#else
#define PFI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PFI_MAX_FILE (32U * 1024U * 1024U)
#define PFI_MAX_CHUNKS 8192U
#define PFI_MAX_TRACKS 2048U
#define PFI_MAX_INDEXES 4096U
#define PFI_MAX_PULSES 2097152U
#define PFI_MAX_TEXT (1024U * 1024U)
#define PFI_MAX_DATA (16U * 1024U * 1024U)
#define PFI_CYLINDERS 1024U
#define PFI_HEADS 2U
#define PFI_TRANSFER 32768U

typedef struct pfi_track_s {
    bool active;
    size_t indexes_member, pulses_member;
    uint32_t indexes[PFI_MAX_INDEXES];
    size_t index_count;
} pfi_track;

static const xx_crc_model pfi_crc_model = {
    32U, UINT64_C(0x1edc6f41), 0U, false, false, 0U, "PCE PFI"
};

static bool pfi_chunk_crc(Abstractformat *format, int64_t offset,
                          const uint8_t header[8], uint32_t length,
                          xx_pd_struct *pd) {
    uint8_t buffer[PFI_TRANSFER], trailer[4];
    xx_crc_context crc;
    uint32_t remaining = length;
    int64_t at = offset + 8;
    if (!xx_crc_context_init(&crc, &pfi_crc_model)) return false;
    xx_crc_context_update(&crc, header, 8U);
    while (remaining) {
        size_t take = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, at, buffer, take)) return false;
        xx_crc_context_update(&crc, buffer, take);
        remaining -= (uint32_t)take;
        at += (int64_t)take;
    }
    return (!pd || !xx_pd_is_stopped(pd)) &&
           pm_read(format, at, trailer, sizeof(trailer)) &&
           (uint32_t)xx_crc_context_final(&crc) == xx_data_get_u32(trailer, 4, 0, true);
}

static int pfi_compare_u32(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static bool pfi_finish_track(pm_stream *stream, pfi_track *track) {
    pm_member *member;
    size_t i, unique = 0U;
    uint8_t *bytes;
    if (!track->active || track->index_count == 0U) return true;
    qsort(track->indexes, track->index_count, sizeof(track->indexes[0]),
          pfi_compare_u32);
    for (i = 0U; i < track->index_count; ++i)
        if (unique == 0U || track->indexes[i] != track->indexes[unique - 1U])
            track->indexes[unique++] = track->indexes[i];
    bytes = (uint8_t *)xx_mem_alloc(unique * 4U);
    if (!bytes) return false;
    for (i = 0U; i < unique; ++i) xx_data_set_u32(bytes + i * 4U, 4, 0, track->indexes[i], false);
    member = &stream->items[track->indexes_member];
    member->memory = bytes;
    member->offset = -1;
    member->size = member->packed_size = (int64_t)(unique * 4U);
    return true;
}

/* One pass validates/counts, then a second pass writes decoded uint32s. */
static bool pfi_decode_pulses(const uint8_t *input, size_t length,
                              uint8_t *output, size_t *count) {
    size_t cursor = 0U, produced = 0U;
    while (cursor < length) {
        uint32_t value, tag = input[cursor++];
        unsigned additional;
        if (tag == 0U) return false; /* PFI reserves 00 as invalid. */
        if (tag <= 3U) {
            if ((size_t)tag + 1U > length - cursor) return false;
            value = 0U;
            for (additional = 0U; additional <= tag; ++additional)
                value = (value << 8U) | input[cursor++];
        } else if (tag <= 7U) {
            if (cursor == length) return false;
            value = ((tag & 3U) << 8U) | input[cursor++];
        } else {
            value = tag;
        }
        if (output) xx_data_set_u32(output + produced * 4U, 4, 0, value, false);
        ++produced;
    }
    *count = produced;
    return true;
}

static bool pfi_add_pulses(Abstractformat *format, pm_stream *stream,
                            pfi_track *track, int64_t at, uint32_t length,
                            uint32_t *total_pulses) {
    pm_member *member = &stream->items[track->pulses_member];
    uint8_t *encoded, *decoded;
    size_t count = 0U, previous;
    if (length > PFI_MAX_DATA) return false;
    encoded = (uint8_t *)xx_mem_alloc(length ? length : 1U);
    if (!encoded) return false;
    if (!pm_read(format, at, encoded, length) ||
        !pfi_decode_pulses(encoded, length, NULL, &count) ||
        count > PFI_MAX_PULSES - *total_pulses ||
        count > (SIZE_MAX - (size_t)member->size) / 4U) {
        xx_mem_free(encoded);
        return false;
    }
    previous = (size_t)member->size;
    if (count != 0U) {
        decoded = (uint8_t *)xx_mem_realloc(member->memory,
                                             previous + count * 4U);
        if (!decoded) {
            xx_mem_free(encoded);
            return false;
        }
        member->memory = decoded;
        if (!pfi_decode_pulses(encoded, length, decoded + previous, &count)) {
            xx_mem_free(encoded);
            return false;
        }
    }
    member->offset = -1;
    member->size += (int64_t)(count * 4U);
    member->packed_size += (int64_t)length;
    *total_pulses += (uint32_t)count;
    xx_mem_free(encoded);
    return true;
}

static bool pfi_start_track(Abstractformat *format, pm_stream *stream,
                             pfi_track *track, const uint8_t fields[12],
                             uint8_t seen[PFI_CYLINDERS * PFI_HEADS / 8U]) {
    uint32_t cylinder = xx_data_get_u32(fields, 4, 0, true), head = xx_data_get_u32(fields + 4U, 4, 0, true);
    uint32_t clock = xx_data_get_u32(fields + 8U, 4, 0, true), index;
    char name[72];
    uint8_t *clock_bytes;
    if (cylinder >= PFI_CYLINDERS || head >= PFI_HEADS || clock == 0U)
        return false;
    index = cylinder * PFI_HEADS + head;
    if (seen[index / 8U] & (uint8_t)(1U << (index & 7U))) return false;
    seen[index / 8U] |= (uint8_t)(1U << (index & 7U));
    (void)xx_rt_snprintf(name, sizeof(name), "track%03u_%u.clock.u32le",
                         cylinder, head);
    if (!pm_add(format, stream, name, 0, 0)) return false;
    clock_bytes = (uint8_t *)xx_mem_alloc(4U);
    if (!clock_bytes) return false;
    xx_data_set_u32(clock_bytes, 4, 0, clock, false);
    stream->items[stream->count - 1U].memory = clock_bytes;
    stream->items[stream->count - 1U].offset = -1;
    stream->items[stream->count - 1U].size = 4;
    stream->items[stream->count - 1U].packed_size = 4;
    (void)xx_rt_snprintf(name, sizeof(name), "track%03u_%u.indexes.u32le",
                         cylinder, head);
    if (!pm_add(format, stream, name, 0, 0)) return false;
    track->indexes_member = stream->count - 1U;
    (void)xx_rt_snprintf(name, sizeof(name), "track%03u_%u.pulses.u32le",
                         cylinder, head);
    if (!pm_add(format, stream, name, 0, 0)) return false;
    track->pulses_member = stream->count - 1U;
    track->index_count = 0U;
    track->active = true;
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    int64_t available = pm_available(format), cursor = 0;
    uint32_t chunks = 0U, tracks = 0U, total_pulses = 0U;
    uint8_t seen[PFI_CYLINDERS * PFI_HEADS / 8U] = {0};
    pfi_track track;
    bool header_seen = false;
    if (available < 28 || available > PFI_MAX_FILE ||
        (pd && xx_pd_is_stopped(pd))) return false;
    xx_mem_zero(&track, sizeof(track));
    while (cursor < available) {
        uint8_t header[8];
        uint32_t length;
        int64_t next;
        if (++chunks > PFI_MAX_CHUNKS || available - cursor < 12 ||
            !pm_read(format, cursor, header, sizeof(header))) return false;
        length = xx_data_get_u32(header + 4U, 4, 0, true);
        if ((uint64_t)length > (uint64_t)(available - cursor - 12))
            return false;
        next = cursor + 12 + (int64_t)length;
        if (!pfi_chunk_crc(format, cursor, header, length, pd))
            return false;
        if (!header_seen) {
            uint8_t version[4];
            if (memcmp(header, "PFI ", 4U) != 0 || length != 4U ||
                !pm_read(format, cursor + 8, version, 4U) ||
                xx_data_get_u32(version, 4, 0, true) != 0U) return false;
            header_seen = true;
        } else if (memcmp(header, "TEXT", 4U) == 0) {
            if (length > PFI_MAX_TEXT ||
                !pm_add(format, stream, "comment.txt", cursor + 8,
                        (int64_t)length)) return false;
        } else if (memcmp(header, "TRAK", 4U) == 0) {
            uint8_t fields[12];
            if (length != 12U || tracks >= PFI_MAX_TRACKS ||
                !pm_read(format, cursor + 8, fields, sizeof(fields)) ||
                !pfi_finish_track(stream, &track) ||
                !pfi_start_track(format, stream, &track, fields, seen))
                return false;
            ++tracks;
        } else if (memcmp(header, "INDX", 4U) == 0) {
            uint32_t i, count = length / 4U;
            uint8_t word[4];
            if (!track.active || length % 4U ||
                count > PFI_MAX_INDEXES - track.index_count) return false;
            for (i = 0U; i < count; ++i) {
                if ((pd && xx_pd_is_stopped(pd)) ||
                    !pm_read(format, cursor + 8 + (int64_t)i * 4,
                             word, 4U)) return false;
                track.indexes[track.index_count++] = xx_data_get_u32(word, 4, 0, true);
            }
        } else if (memcmp(header, "DATA", 4U) == 0) {
            if (!track.active || !pfi_add_pulses(format, stream, &track,
                                      cursor + 8, length, &total_pulses))
                return false;
        } else if (memcmp(header, "END ", 4U) == 0) {
            if (length != 0U || tracks == 0U || next != available ||
                !pfi_finish_track(stream, &track)) return false;
            stream->size = available;
            return true;
        } else if (memcmp(header, "PFI ", 4U) == 0) {
            return false; /* The file header is unique. */
        } /* Unknown CRC-checked future chunks are safely skipped. */
        cursor = next;
    }
    return false;
}

void xx_pce_pfi_init(xx_pce_pfi *reader, xx_io_device *device,
                     int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, PFI_FILE_TYPE, "pfi");
}

xx_pce_pfi *xx_pce_pfi_create(xx_io_device *device, int64_t base_address) {
    xx_pce_pfi *reader = (xx_pce_pfi *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_pfi_init(reader, device, base_address);
    return reader;
}

void xx_pce_pfi_destroy(xx_pce_pfi *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_pfi_free(xx_pce_pfi *reader) {
    if (reader) {
        xx_pce_pfi_destroy(reader);
        xx_mem_free(reader);
    }
}
