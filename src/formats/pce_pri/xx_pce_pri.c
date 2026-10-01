/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native reader for Hampa Hug's PCE PRI version-0 chunk grammar.
 * PCE is the independent test producer; no PCE code is imported here.
 */
#include "xxfclib/formats/pce_pri/xx_pce_pri.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include <string.h>

#ifdef PCE_PRI
#define PCE_PRI_FILE_TYPE XX_FILE_TYPE_PCE_PRI
#else
#define PCE_PRI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PRI_MAX_FILE (64U * 1024U * 1024U)
#define PRI_MAX_CHUNKS 8192U
#define PRI_MAX_TRACKS 2048U
#define PRI_MAX_TRACK_BITS (16U * 1024U * 1024U)
#define PRI_MAX_TEXT (1024U * 1024U)
#define PRI_CYLINDERS 1024U
#define PRI_HEADS 2U
#define PRI_TRANSFER 32768U

static uint32_t pri_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static const xx_crc_model pri_crc_model = {
    32U, UINT64_C(0x1edc6f41), 0U, false, false, 0U, "PCE PRI"
};

/* CRC validation is streamed: no chunk payload is retained by the parser. */
static bool pri_chunk_crc(Abstractformat *format, int64_t offset,
                          const uint8_t header[8], uint32_t length,
                          xx_pd_struct *pd) {
    uint8_t buffer[PRI_TRANSFER];
    uint8_t trailer[4];
    xx_crc_context crc;
    uint32_t remaining = length;
    int64_t at = offset + 8;
    if (!xx_crc_context_init(&crc, &pri_crc_model)) return false;
    xx_crc_context_update(&crc, header, 8U);
    while (remaining != 0U) {
        size_t take = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, at, buffer, take)) return false;
        xx_crc_context_update(&crc, buffer, take);
        remaining -= (uint32_t)take;
        at += (int64_t)take;
    }
    return (!pd || !xx_pd_is_stopped(pd)) &&
           pm_read(format, at, trailer, sizeof(trailer)) &&
           (uint32_t)xx_crc_context_final(&crc) == pri_be32(trailer);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    int64_t available = pm_available(format);
    int64_t cursor = 0;
    uint8_t seen[PRI_CYLINDERS * PRI_HEADS / 8U] = {0};
    size_t current = 0U;
    uint32_t expected = 0U, chunks = 0U, tracks = 0U, texts = 0U;
    bool pending = false, have_track = false, header_seen = false;
    if (available < 32 || available > PRI_MAX_FILE ||
        (pd && xx_pd_is_stopped(pd))) return false;
    while (cursor < available) {
        uint8_t header[8];
        uint32_t length;
        int64_t next;
        if (++chunks > PRI_MAX_CHUNKS || available - cursor < 12 ||
            !pm_read(format, cursor, header, sizeof(header))) return false;
        length = pri_be32(header + 4U);
        if ((uint64_t)length > (uint64_t)(available - cursor - 12))
            return false;
        next = cursor + 12 + (int64_t)length;
        if (!pri_chunk_crc(format, cursor, header, length, pd))
            return false;
        if (!header_seen) {
            uint8_t version[4];
            if (memcmp(header, "PRI ", 4U) != 0 || length != 4U ||
                !pm_read(format, cursor + 8, version, sizeof(version)) ||
                version[0] != 0U || version[1] != 0U ||
                version[2] != 0U || version[3] != 0U)
                return false;
            header_seen = true;
        } else if (memcmp(header, "TEXT", 4U) == 0) {
            char name[48];
            if (texts == 0U) (void)xx_rt_snprintf(name, sizeof(name), "comment.txt");
            else (void)xx_rt_snprintf(name, sizeof(name), "comment-%04u.txt", texts);
            if (pending || length > PRI_MAX_TEXT ||
                !pm_add(format, stream, name, cursor + 8,
                        (int64_t)length)) return false;
            ++texts;
        } else if (memcmp(header, "TRAK", 4U) == 0) {
            uint8_t fields[16];
            uint32_t cylinder, head, bits, clock, index;
            char name[64];
            if (pending || length != 16U || tracks >= PRI_MAX_TRACKS ||
                !pm_read(format, cursor + 8, fields, sizeof(fields)))
                return false;
            cylinder = pri_be32(fields);
            head = pri_be32(fields + 4U);
            bits = pri_be32(fields + 8U);
            clock = pri_be32(fields + 12U);
            if (cylinder >= PRI_CYLINDERS || head >= PRI_HEADS ||
                bits > PRI_MAX_TRACK_BITS || clock == 0U) return false;
            index = cylinder * PRI_HEADS + head;
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
            if (!have_track || !pending || length != expected)
                return false;
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
            /* PCE may add metadata chunks; CRC and extent are still checked.
             * A pending non-empty track cannot be silently skipped. */
            if (pending) return false;
        }
        cursor = next;
    }
    return false;
}

void xx_pce_pri_init(xx_pce_pri *reader, xx_io_device *device,
                     int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, PCE_PRI_FILE_TYPE, "pri");
}

xx_pce_pri *xx_pce_pri_create(xx_io_device *device, int64_t base_address) {
    xx_pce_pri *reader = (xx_pce_pri *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_pri_init(reader, device, base_address);
    return reader;
}

void xx_pce_pri_destroy(xx_pce_pri *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_pri_free(xx_pce_pri *reader) {
    if (reader) {
        xx_pce_pri_destroy(reader);
        xx_mem_free(reader);
    }
}
