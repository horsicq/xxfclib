/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native PCE PBIT v0 reader. Original PCE is used only for independent tests.
 */
#include "xxfclib/formats/pce_pbit/xx_pce_pbit.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include <string.h>
#include "xxfclib/data/xx_data.h"

#ifdef PCE_PBIT
#define PBIT_FILE_TYPE XX_FILE_TYPE_PCE_PBIT
#else
#define PBIT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PBIT_MAX_FILE (64U * 1024U * 1024U)
#define PBIT_MAX_CHUNKS 8192U
#define PBIT_MAX_TRACKS 2048U
#define PBIT_MAX_TRACK_BITS (16U * 1024U * 1024U)
#define PBIT_MAX_TEXT (1024U * 1024U)
#define PBIT_CYLINDERS 1024U
#define PBIT_HEADS 2U
#define PBIT_TRANSFER 32768U

static const xx_crc_model pbit_crc_model = {
    32U, UINT64_C(0x1edc6f41), 0U, false, false, 0U, "PCE PBIT"
};

static bool pbit_chunk_crc(Abstractformat *format, int64_t at,
                           const uint8_t header[8], uint32_t length,
                           xx_pd_struct *pd) {
    uint8_t buffer[PBIT_TRANSFER], trailer[4];
    uint32_t remaining = length;
    xx_crc_context crc;
    int64_t cursor = at + 8;
    if (!xx_crc_context_init(&crc, &pbit_crc_model)) return false;
    xx_crc_context_update(&crc, header, 8U);
    while (remaining) {
        size_t take = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, cursor, buffer, take)) return false;
        xx_crc_context_update(&crc, buffer, take);
        cursor += (int64_t)take;
        remaining -= (uint32_t)take;
    }
    return (!pd || !xx_pd_is_stopped(pd)) &&
           pm_read(format, cursor, trailer, sizeof(trailer)) &&
           (uint32_t)xx_crc_context_final(&crc) == xx_data_get_u32(trailer, 4, 0, true);
}

/* PCE's PBIT loader strips surrounding CR/LF/NUL and folds line endings. */
static bool pbit_comment(Abstractformat *format, pm_stream *stream,
                         int64_t at, uint32_t length, uint32_t index) {
    uint8_t *raw, *decoded;
    size_t first = 0U, last = length, i, used = 0U;
    char name[48];
    if (length > PBIT_MAX_TEXT) return false;
    raw = (uint8_t *)xx_mem_alloc(length ? length : 1U);
    if (!raw) return false;
    if (!pm_read(format, at, raw, length)) {
        xx_mem_free(raw);
        return false;
    }
    while (first < last && (raw[first] == '\r' || raw[first] == '\n' ||
                            raw[first] == 0U)) ++first;
    while (last > first && (raw[last - 1U] == '\r' ||
                            raw[last - 1U] == '\n' ||
                            raw[last - 1U] == 0U)) --last;
    if (last == first) {
        xx_mem_free(raw);
        return true;
    }
    decoded = (uint8_t *)xx_mem_alloc(last - first);
    if (!decoded) {
        xx_mem_free(raw);
        return false;
    }
    for (i = first; i < last; ++i) {
        if (raw[i] == '\r') {
            if (i + 1U < last && raw[i + 1U] == '\n') ++i;
            decoded[used++] = '\n';
        } else decoded[used++] = raw[i];
    }
    xx_mem_free(raw);
    if (index == 0U) (void)xx_rt_snprintf(name, sizeof(name), "comment.txt");
    else (void)xx_rt_snprintf(name, sizeof(name), "comment-%04u.txt", index);
    if (!pm_add(format, stream, name, 0, 0)) {
        xx_mem_free(decoded);
        return false;
    }
    stream->items[stream->count - 1U].memory = decoded;
    stream->items[stream->count - 1U].offset = -1;
    stream->items[stream->count - 1U].size = (int64_t)used;
    stream->items[stream->count - 1U].packed_size = (int64_t)length;
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    int64_t available = pm_available(format), cursor = 0;
    uint8_t seen[PBIT_CYLINDERS * PBIT_HEADS / 8U] = {0};
    uint32_t chunks = 0U, tracks = 0U, comments = 0U;
    uint32_t expected = 0U;
    size_t current = 0U;
    bool header_seen = false, have_track = false, pending = false;
    if (available < 32 || available > PBIT_MAX_FILE ||
        (pd && xx_pd_is_stopped(pd))) return false;
    while (cursor < available) {
        uint8_t header[8];
        uint32_t length;
        int64_t next;
        if (++chunks > PBIT_MAX_CHUNKS || available - cursor < 12 ||
            !pm_read(format, cursor, header, sizeof(header))) return false;
        length = xx_data_get_u32(header + 4U, 4, 0, true);
        if ((uint64_t)length > (uint64_t)(available - cursor - 12))
            return false;
        next = cursor + 12 + (int64_t)length;
        if (!pbit_chunk_crc(format, cursor, header, length, pd))
            return false;
        if (!header_seen) {
            uint8_t fields[8];
            if (memcmp(header, "PBIT", 4U) != 0 || length != 8U ||
                !pm_read(format, cursor + 8, fields, sizeof(fields)) ||
                xx_data_get_u32(fields, 4, 0, true) != 0U) return false;
            header_seen = true;
        } else if (memcmp(header, "TEXT", 4U) == 0) {
            if (pending || !pbit_comment(format, stream, cursor + 8,
                                         length, comments)) return false;
            ++comments;
        } else if (memcmp(header, "TRAK", 4U) == 0) {
            uint8_t fields[20];
            uint32_t cylinder, head, bits, clock, index;
            char name[64];
            if (pending || length != 20U || tracks >= PBIT_MAX_TRACKS ||
                !pm_read(format, cursor + 8, fields, sizeof(fields)))
                return false;
            cylinder = xx_data_get_u32(fields, 4, 0, true);
            head = xx_data_get_u32(fields + 4U, 4, 0, true);
            bits = xx_data_get_u32(fields + 8U, 4, 0, true);
            clock = xx_data_get_u32(fields + 12U, 4, 0, true);
            if (cylinder >= PBIT_CYLINDERS || head >= PBIT_HEADS ||
                bits > PBIT_MAX_TRACK_BITS || clock == 0U) return false;
            index = cylinder * PBIT_HEADS + head;
            if ((seen[index / 8U] & (uint8_t)(1U << (index & 7U))) != 0U)
                return false;
            seen[index / 8U] |= (uint8_t)(1U << (index & 7U));
            (void)xx_rt_snprintf(name, sizeof(name), "track%03u_%u.bit",
                                 cylinder, head);
            if (!pm_add(format, stream, name, 0, 0)) return false;
            current = stream->count - 1U;
            expected = (bits + 7U) / 8U;
            pending = expected != 0U;
            have_track = true;
            ++tracks;
        } else if (memcmp(header, "DATA", 4U) == 0) {
            if (!have_track || !pending || length != expected) return false;
            stream->items[current].offset = format->base_address + cursor + 8;
            stream->items[current].size = (int64_t)length;
            stream->items[current].packed_size = (int64_t)length;
            pending = false;
        } else if (memcmp(header, "END ", 4U) == 0) {
            if (length != 0U || pending || tracks == 0U ||
                next != available) return false;
            stream->size = available;
            return true;
        } else {
            if (pending || memcmp(header, "PBIT", 4U) == 0) return false;
            /* Unknown future chunks remain CRC/extent checked. */
        }
        cursor = next;
    }
    return false;
}

void xx_pce_pbit_init(xx_pce_pbit *reader, xx_io_device *device,
                      int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, PBIT_FILE_TYPE, "pbit");
}

xx_pce_pbit *xx_pce_pbit_create(xx_io_device *device, int64_t base_address) {
    xx_pce_pbit *reader = (xx_pce_pbit *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_pbit_init(reader, device, base_address);
    return reader;
}

void xx_pce_pbit_destroy(xx_pce_pbit *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_pbit_free(xx_pce_pbit *reader) {
    if (reader) {
        xx_pce_pbit_destroy(reader);
        xx_mem_free(reader);
    }
}
