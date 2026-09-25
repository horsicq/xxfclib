/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AP4, the container used by a family of hardware reading pens.
 *
 *   table of contents header, 5 bytes:  00 01 NN 01 NN   (NN = record count)
 *   table of contents record, 9 bytes:  u32 BE start | u32 BE size | u8 flag
 *
 * A file holds a chain of these tables near the start, then the members. There
 * is no magic anywhere, which is the whole difficulty: the five-byte header
 * pattern alone hits by chance about once in 65,000 random files, so it cannot
 * be the gate. What makes identification sound is everything layered on top --
 *
 *   - the first non-empty table must start within the first 64 KiB;
 *   - every record's extent must lie inside the file and must not overlap any
 *     table in the chain;
 *   - no record may start at offset 0, which would collide with the prefix;
 *   - every table must end at or before the first member, so the table region
 *     really does precede the data;
 *   - and the first member that is not entirely zero must decode as MPEG audio.
 *
 * That last rule is what turns a structural coincidence into a decision, and
 * it is evaluated identically whether the caller is probing or listing, so the
 * detector and the reader can never disagree about a file.
 *
 * Members are stored verbatim or XOR-obfuscated with one repeating byte. The
 * key is recovered from the assumption that the plaintext starts with an MPEG
 * sync byte (0xFF), then confirmed by parsing a second frame header at exactly
 * the distance the first frame's own bitrate and sample rate predict. A single
 * header would be far too easy to hit; two agreeing ones at a computed
 * distance are not.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ap4/xx_ap4.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>  /* SEEK_SET only: formatting goes through xx_rt */

#define XX_AP4_TOC_HEADER_SIZE 5
#define XX_AP4_TOC_RECORD_SIZE 9
/* One table, one record and one MPEG frame header. */
#define XX_AP4_MIN_INPUT_SIZE (XX_AP4_TOC_HEADER_SIZE + XX_AP4_TOC_RECORD_SIZE + 4)
/* The first non-empty table must begin inside this window. The prefix length
 * varies by vendor, so a fixed offset cannot be required; one buffered read is
 * comfortably past any plausible prefix. */
#define XX_AP4_FIRST_TOC_WINDOW (64 * 1024)
/* Bomb guard on how far behind the first table the chain may run. */
#define XX_AP4_MAX_TOC_REGION_SIZE ((int64_t)64 * 1024 * 1024)
#define XX_AP4_MAX_TOCS 4096
#define XX_AP4_MAX_MEMBERS 65535
/* Must cover the largest legal MPEG frame (2881) plus a second header, so the
 * confirmation never runs off the end of the probe. */
#define XX_AP4_PROBE_SIZE (64 * 1024)
#define XX_AP4_MPEG_HEADER_SIZE 4
#define XX_AP4_SCAN_CHUNK_SIZE (64 * 1024)
#define XX_AP4_COPY_BUFFER_SIZE (64 * 1024)
#define XX_AP4_MPEG_SYNC_BYTE 0xFFU

typedef struct xx_ap4_member_s {
    int64_t offset;
    int64_t size;
    uint8_t flag;
    uint8_t xor_key; /* 0 = stored verbatim */
    bool is_mp3;
    char *name;
} xx_ap4_member;

typedef struct xx_ap4_toc_s {
    int64_t offset;
    int64_t size;
} xx_ap4_toc;

typedef struct xx_ap4_stream_s {
    xx_ap4_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    int64_t prefix_size;
    int64_t header_size;
    uint32_t toc_count;
} xx_ap4_stream;

/* The MPEG frame header fields the length formula and the agreement test
 * need. */
typedef struct xx_ap4_mpeg_header_s {
    int version_bits; /* 0 = MPEG 2.5, 1 = reserved, 2 = MPEG 2, 3 = MPEG 1 */
    int layer_bits;
    int bitrate_index;
    int sample_index;
    int padding;
} xx_ap4_mpeg_header;

static void xx_ap4_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ap4_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static uint32_t xx_ap4_read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static bool xx_ap4_is_toc_header(const uint8_t *data) {
    return data[0] == 0x00U && data[1] == 0x01U && data[3] == 0x01U &&
           data[2] == data[4];
}

static bool xx_ap4_is_all_zero(const uint8_t *data, size_t size) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (data[index] != 0U) return false;
    }
    return true;
}

static bool xx_ap4_ranges_overlap(int64_t a_offset, int64_t a_size,
                                  int64_t b_offset, int64_t b_size) {
    return a_offset < b_offset + b_size && b_offset < a_offset + a_size;
}

/* --------------------------------------------------------------- MPEG --- */

static bool xx_ap4_parse_mpeg_header(const uint8_t *data, uint8_t key,
                                     xx_ap4_mpeg_header *out) {
    uint8_t byte0 = (uint8_t)(data[0] ^ key);
    uint8_t byte1 = (uint8_t)(data[1] ^ key);
    uint8_t byte2 = (uint8_t)(data[2] ^ key);
    int version_bits;
    int layer_bits;
    int bitrate_index;
    int sample_index;

    if (byte0 != XX_AP4_MPEG_SYNC_BYTE) return false;
    if ((byte1 & 0xE0U) != 0xE0U) return false;

    version_bits = (byte1 >> 3) & 3;
    if (version_bits == 1) return false; /* reserved */
    layer_bits = (byte1 >> 1) & 3;
    if (layer_bits == 0) return false; /* reserved */
    bitrate_index = byte2 >> 4;
    /* 0 is free format, which reading pens never emit; 15 is invalid. */
    if (bitrate_index < 1 || bitrate_index > 14) return false;
    sample_index = (byte2 >> 2) & 3;
    if (sample_index == 3) return false;

    out->version_bits = version_bits;
    out->layer_bits = layer_bits;
    out->bitrate_index = bitrate_index;
    out->sample_index = sample_index;
    out->padding = (byte2 >> 1) & 1;
    return true;
}

/* Standard MPEG 1 / 2 / 2.5 frame length, or 0 when the fields do not
 * describe a frame. */
static int64_t xx_ap4_mpeg_frame_length(const xx_ap4_mpeg_header *header) {
    /* Rows: Layer I, II, III. Columns: bitrate index 0..14, in kbps. */
    static const int bitrate_mpeg1[3][15] = {
        {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448},
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}};
    static const int bitrate_mpeg2[3][15] = {
        {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
    /* Rows: version bits 0..3 (MPEG 2.5, reserved, MPEG 2, MPEG 1). */
    static const int sample_rate[4][3] = {{11025, 12000, 8000},
                                          {0, 0, 0},
                                          {22050, 24000, 16000},
                                          {44100, 48000, 32000}};
    int layer_row;
    bool is_mpeg1;
    int64_t bitrate;
    int64_t rate;

    if (header->version_bits < 0 || header->version_bits > 3 ||
        header->version_bits == 1 || header->layer_bits < 1 ||
        header->layer_bits > 3 || header->bitrate_index < 1 ||
        header->bitrate_index > 14 || header->sample_index < 0 ||
        header->sample_index > 2) {
        return 0;
    }
    /* Layer bits 3/2/1 mean Layer I/II/III, so they index the table in
     * reverse. */
    layer_row = 3 - header->layer_bits;
    is_mpeg1 = header->version_bits == 3;
    bitrate = (int64_t)(is_mpeg1 ? bitrate_mpeg1[layer_row][header->bitrate_index]
                                 : bitrate_mpeg2[layer_row][header->bitrate_index]) *
              1000;
    rate = sample_rate[header->version_bits][header->sample_index];
    if (bitrate <= 0 || rate <= 0) return 0;

    if (layer_row == 0) {
        return ((12 * bitrate / rate) + header->padding) * 4;
    }
    if (layer_row == 1) {
        return (144 * bitrate / rate) + header->padding;
    }
    return ((is_mpeg1 ? 144 : 72) * bitrate / rate) + header->padding;
}

static bool xx_ap4_headers_agree(const xx_ap4_mpeg_header *first,
                                 const xx_ap4_mpeg_header *second) {
    return first->version_bits == second->version_bits &&
           first->layer_bits == second->layer_bits &&
           first->sample_index == second->sample_index;
}

/*
 * The MPEG start test under one XOR hypothesis; key 0 is the plain case.
 */
static bool xx_ap4_is_mp3_start(const uint8_t *data, size_t probe_size,
                                int64_t member_size, uint8_t key) {
    xx_ap4_mpeg_header first;
    xx_ap4_mpeg_header second;
    int64_t frame_length;

    if (!data || probe_size < (size_t)XX_AP4_MPEG_HEADER_SIZE) return false;
    if (!xx_ap4_parse_mpeg_header(data, key, &first)) return false;
    frame_length = xx_ap4_mpeg_frame_length(&first);
    if (frame_length <= XX_AP4_MPEG_HEADER_SIZE) return false;

    /* Under an XOR hypothesis the first byte matches by construction, so a
     * member too small to hold the frame it claims is evidence of nothing --
     * refuse it rather than XOR-garble an opaque blob into an .mp3. The plain
     * path keeps the reference's single-header rule. */
    if (key != 0U && member_size < frame_length) return false;
    /* Too short for a second header: the single header stands. */
    if (member_size < frame_length + XX_AP4_MPEG_HEADER_SIZE) return true;
    /* The probe always covers the largest legal frame, so a probe too short
     * here means a short read, and that fails closed. */
    if (probe_size < (size_t)(frame_length + XX_AP4_MPEG_HEADER_SIZE)) {
        return false;
    }
    if (!xx_ap4_parse_mpeg_header(data + frame_length, key, &second)) {
        return false;
    }
    return xx_ap4_headers_agree(&first, &second);
}

/*
 * Plain MP3 (frame header or ID3v2 tag) -> mp3, key 0.
 * XOR-obfuscated MP3 -> mp3, key = first byte ^ 0xFF.
 * Anything else -> opaque blob, key 0.
 */
static void xx_ap4_classify_member(const uint8_t *probe, size_t probe_size,
                                   int64_t member_size, bool *is_mp3,
                                   uint8_t *key_out) {
    uint8_t key;

    *is_mp3 = false;
    *key_out = 0U;

    if (probe_size >= 3U && probe[0] == 'I' && probe[1] == 'D' &&
        probe[2] == '3') {
        *is_mp3 = true;
        return;
    }
    if (probe_size < (size_t)XX_AP4_MPEG_HEADER_SIZE) return;

    if (xx_ap4_is_mp3_start(probe, probe_size, member_size, 0U)) {
        *is_mp3 = true;
        return;
    }
    /* The hypothesis is that the plaintext opens with the sync byte. A member
     * that already starts with 0xFF but failed the plain test would yield key
     * 0, which is not a hypothesis at all. */
    key = (uint8_t)(probe[0] ^ XX_AP4_MPEG_SYNC_BYTE);
    if (key == 0U) return;
    if (xx_ap4_is_mp3_start(probe, probe_size, member_size, key)) {
        *is_mp3 = true;
        *key_out = key;
    }
}

/* ------------------------------------------------------------- parsing -- */

static void xx_ap4_stream_free(void *pointer) {
    xx_ap4_stream *stream = (xx_ap4_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Scan [from, end) for a table header, buffering so a run of empty tables does
 * not cost one device read per five bytes. Returns the offset, or -1. */
static int64_t xx_ap4_find_toc(Abstractformat *self, int64_t from, int64_t end,
                               int64_t input_size, uint8_t *record_count,
                               uint8_t *cache, int64_t *cache_offset,
                               size_t *cache_size, xx_pd_struct *pd) {
    int64_t position = from;

    if (from < 0 || end > input_size) return -1;
    while (position + XX_AP4_TOC_HEADER_SIZE <= end) {
        int64_t window_end;
        int64_t cached_end =
            *cache_offset < 0 ? -1 : *cache_offset + (int64_t)*cache_size;

        if (pd && xx_pd_is_stopped(pd)) return -1;
        if (*cache_offset < 0 || position < *cache_offset ||
            position + XX_AP4_TOC_HEADER_SIZE > cached_end) {
            int64_t available = input_size - position;
            size_t chunk = (size_t)(available < XX_AP4_SCAN_CHUNK_SIZE
                                        ? available
                                        : XX_AP4_SCAN_CHUNK_SIZE);
            if (chunk < (size_t)XX_AP4_TOC_HEADER_SIZE) return -1;
            if (!xx_ap4_read_at(self, position, cache, chunk)) return -1;
            *cache_offset = position;
            *cache_size = chunk;
        }
        window_end = *cache_offset + (int64_t)*cache_size;
        while (position + XX_AP4_TOC_HEADER_SIZE <= end &&
               position + XX_AP4_TOC_HEADER_SIZE <= window_end) {
            const uint8_t *data = cache + (position - *cache_offset);
            if (xx_ap4_is_toc_header(data)) {
                *record_count = data[4];
                return position;
            }
            ++position;
        }
    }
    return -1;
}

/*
 * gate_only stops after the first member that is not entirely zero has been
 * classified: that member carries the decision, and the rest are only material
 * for a listing. The full parse additionally drops members whose whole extent
 * is zero and classifies every survivor.
 */
static xx_ap4_stream *xx_ap4_parse(Abstractformat *self, bool gate_only,
                                   xx_pd_struct *pd) {
    xx_ap4_stream *stream = NULL;
    xx_ap4_toc *tocs = NULL;
    xx_ap4_member *members = NULL;
    uint8_t *cache = NULL;
    uint8_t *probe = NULL;
    size_t toc_count = 0U;
    size_t member_count = 0U;
    size_t index;
    int64_t cache_offset = -1;
    size_t cache_size = 0U;
    int64_t input_size;
    int64_t scan_position = 0;
    int64_t scan_limit;
    int64_t first_toc_offset = -1;
    int64_t archive_size = 0;
    int64_t prefix_size = 0;
    int64_t header_size = 0;
    bool first_probed_seen = false;
    size_t kept = 0U;

    if (!self || !self->device || self->base_address != 0) {
        /* The table offsets are absolute file offsets, so a non-zero base
         * address would need every one of them rebased; the reference has no
         * such case and inventing one would be guesswork. */
        return NULL;
    }
    input_size = xx_io_total_size(self->device);
    if (input_size < XX_AP4_MIN_INPUT_SIZE) return NULL;

    cache = (uint8_t *)xx_mem_alloc(XX_AP4_SCAN_CHUNK_SIZE);
    tocs = (xx_ap4_toc *)xx_mem_alloc(sizeof(*tocs) * XX_AP4_MAX_TOCS);
    if (!cache || !tocs) goto fail;

    /* No table may lie at or above the lowest member start. */
    scan_limit = input_size;

    for (;;) {
        bool is_first = first_toc_offset < 0;
        int64_t search_end = scan_limit;
        int64_t found;
        uint8_t record_count = 0U;
        int64_t records_size;
        int64_t toc_size;
        int64_t toc_end;
        uint8_t *records;
        int i;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (is_first) {
            if (search_end > XX_AP4_FIRST_TOC_WINDOW) {
                search_end = XX_AP4_FIRST_TOC_WINDOW;
            }
        } else if (search_end > first_toc_offset + XX_AP4_MAX_TOC_REGION_SIZE) {
            search_end = first_toc_offset + XX_AP4_MAX_TOC_REGION_SIZE;
        }

        found = xx_ap4_find_toc(self, scan_position, search_end, input_size,
                                &record_count, cache, &cache_offset,
                                &cache_size, pd);
        if (found < 0) {
            if (is_first) goto fail;
            break;
        }
        if (record_count == 0U) {
            /* An empty table is consumed but not counted. */
            scan_position = found + XX_AP4_TOC_HEADER_SIZE;
            continue;
        }

        records_size = (int64_t)XX_AP4_TOC_RECORD_SIZE * record_count;
        toc_size = XX_AP4_TOC_HEADER_SIZE + records_size;
        toc_end = found + toc_size;
        /* A truncated table, or one reaching into member data, is nonsense. */
        if (toc_end > input_size || toc_end > scan_limit) goto fail;

        records = (uint8_t *)xx_mem_alloc((size_t)records_size);
        if (!records) goto fail;
        if (!xx_ap4_read_at(self, found + XX_AP4_TOC_HEADER_SIZE, records,
                            (size_t)records_size)) {
            xx_mem_free(records);
            goto fail;
        }

        if (toc_count >= XX_AP4_MAX_TOCS) {
            xx_mem_free(records);
            goto fail;
        }
        tocs[toc_count].offset = found;
        tocs[toc_count].size = toc_size;
        ++toc_count;

        for (i = 0; i < (int)record_count; ++i) {
            const uint8_t *record = records + ((size_t)i * XX_AP4_TOC_RECORD_SIZE);
            int64_t start = (int64_t)xx_ap4_read_be32(record);
            int64_t size = (int64_t)xx_ap4_read_be32(record + 4);
            uint8_t flag = record[8];
            size_t existing;
            size_t j;
            bool duplicate = false;
            xx_ap4_member *grown;

            if (size == 0) continue;
            if (start > input_size - size) continue;
            /* Offset 0 would overlap the prefix and the table region; the
             * reference never sees such a record in a real file. */
            if (start == 0) {
                xx_mem_free(records);
                goto fail;
            }
            for (existing = 0U; existing < member_count; ++existing) {
                if (members[existing].offset == start &&
                    members[existing].size == size) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            for (j = 0U; j < toc_count; ++j) {
                if (xx_ap4_ranges_overlap(start, size, tocs[j].offset,
                                          tocs[j].size)) {
                    xx_mem_free(records);
                    goto fail;
                }
            }
            if (member_count >= XX_AP4_MAX_MEMBERS) {
                xx_mem_free(records);
                goto fail;
            }
            grown = (xx_ap4_member *)xx_mem_realloc(
                members, sizeof(*members) * (member_count + 1U));
            if (!grown) {
                xx_mem_free(records);
                goto fail;
            }
            members = grown;
            xx_mem_zero(&members[member_count], sizeof(members[member_count]));
            members[member_count].offset = start;
            members[member_count].size = size;
            members[member_count].flag = flag;
            ++member_count;

            if (start < scan_limit) scan_limit = start;
            if (start + size > archive_size) archive_size = start + size;
        }
        xx_mem_free(records);

        if (is_first) {
            first_toc_offset = found;
            prefix_size = found;
        }
        scan_position = toc_end;
    }

    if (toc_count == 0U || member_count == 0U) goto fail;
    /* The whole table region precedes every member. */
    for (index = 0U; index < toc_count; ++index) {
        if (tocs[index].offset + tocs[index].size > scan_limit) goto fail;
    }
    header_size = tocs[toc_count - 1U].offset + tocs[toc_count - 1U].size;
    if (header_size > archive_size) archive_size = header_size;
    if (archive_size > input_size) goto fail;

    /* Classification. The first member whose probe is not all zero must be an
     * MP3, and that rule is evaluated on the same probe in both modes so the
     * gate and the listing can never disagree about a file. */
    probe = (uint8_t *)xx_mem_alloc(XX_AP4_PROBE_SIZE);
    if (!probe) goto fail;

    for (index = 0U; index < member_count; ++index) {
        xx_ap4_member *member = &members[index];
        size_t probe_size = (size_t)(member->size < XX_AP4_PROBE_SIZE
                                         ? member->size
                                         : XX_AP4_PROBE_SIZE);
        bool probe_null;
        bool all_null;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_ap4_read_at(self, member->offset, probe, probe_size)) goto fail;
        probe_null = xx_ap4_is_all_zero(probe, probe_size);
        all_null = probe_null;

        if (!gate_only && probe_null && member->size > (int64_t)probe_size) {
            /* Keep reading only while every byte so far is zero, so the cost
             * is bounded by the zero prefix rather than by the member. */
            int64_t position = member->offset + (int64_t)probe_size;
            int64_t end = member->offset + member->size;
            uint8_t *chunk = (uint8_t *)xx_mem_alloc(XX_AP4_COPY_BUFFER_SIZE);
            if (!chunk) goto fail;
            while (position < end) {
                size_t take = (size_t)(end - position < XX_AP4_COPY_BUFFER_SIZE
                                           ? end - position
                                           : XX_AP4_COPY_BUFFER_SIZE);
                if (pd && xx_pd_is_stopped(pd)) {
                    xx_mem_free(chunk);
                    goto fail;
                }
                if (!xx_ap4_read_at(self, position, chunk, take)) {
                    xx_mem_free(chunk);
                    goto fail;
                }
                if (!xx_ap4_is_all_zero(chunk, take)) {
                    all_null = false;
                    break;
                }
                position += (int64_t)take;
            }
            xx_mem_free(chunk);
        }
        if (all_null) continue;

        if (!probe_null) {
            xx_ap4_classify_member(probe, probe_size, member->size,
                                   &member->is_mp3, &member->xor_key);
            if (!first_probed_seen) {
                first_probed_seen = true;
                /* The decision: a container whose first real member is not
                 * audio is not an AP4 container. */
                if (!member->is_mp3) goto fail;
            }
        }
        if (index != kept) members[kept] = *member;
        ++kept;
        if (gate_only && first_probed_seen) break;
    }
    if (!first_probed_seen || kept == 0U) goto fail;
    member_count = kept;

    /* Names are synthesised from the extent, exactly as the reference does
     * when the container's own file name is unavailable. */
    {
        int hex_digits = 1;
        int64_t scratch = input_size;
        while (scratch >= 16) {
            ++hex_digits;
            scratch /= 16;
        }
        for (index = 0U; index < member_count; ++index) {
            char buffer[128];
            xx_ap4_member *member = &members[index];
            (void)xx_rt_snprintf(buffer, sizeof(buffer),
                           "ap4 0x%0*llx-0x%0*llx (%lld).%s", hex_digits,
                           (unsigned long long)member->offset, hex_digits,
                           (unsigned long long)(member->offset + member->size),
                           (long long)member->size,
                           member->is_mp3 ? "mp3" : "unk");
            member->name = xx_str_dup(buffer);
            if (!member->name) goto fail;
        }
    }

    stream = (xx_ap4_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = members;
    stream->count = member_count;
    stream->index = 0U;
    stream->archive_size = archive_size;
    stream->prefix_size = prefix_size;
    stream->header_size = header_size;
    stream->toc_count = (uint32_t)toc_count;

    xx_mem_free(probe);
    xx_mem_free(tocs);
    xx_mem_free(cache);
    return stream;

fail:
    if (members) {
        for (index = 0U; index < member_count; ++index) {
            xx_str_free(members[index].name);
        }
        xx_mem_free(members);
    }
    xx_mem_free(probe);
    xx_mem_free(tocs);
    xx_mem_free(cache);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ap4_init(xx_ap4 *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* Table records store their offsets and sizes big-endian. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_AP4;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ap4");
    xx_format_set_extension(&archive->format, "ap4");
    archive->format.check_is_valid = xx_ap4_check_is_valid;
    archive->format.handle_base_info = xx_ap4_handle_base_info;
    archive->format.get_format_size = xx_ap4_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ap4_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ap4_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ap4_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ap4_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ap4_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ap4_free_archive_records_reading;
    archive->format.destroy = xx_ap4_vtable_destroy;
}

xx_ap4 *xx_ap4_create(xx_io_device *device, int64_t base_address) {
    xx_ap4 *archive = (xx_ap4 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ap4_init(archive, device, base_address);
    return archive;
}

void xx_ap4_destroy(xx_ap4 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches back through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->toc_count = 0U;
}

void xx_ap4_free(xx_ap4 *archive) {
    if (!archive) return;
    xx_ap4_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ap4_vtable_destroy(Abstractformat *self) {
    xx_ap4_destroy((xx_ap4 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ap4_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ap4_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ap4_parse(self, true, pd);
    if (!stream) return false;
    xx_ap4_stream_free(stream);
    return true;
}

bool xx_ap4_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ap4 *archive = (xx_ap4 *)self;
    xx_ap4_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ap4_parse(self, false, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size - self->base_address;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->prefix_size = stream->prefix_size;
    archive->header_size = stream->header_size;
    archive->toc_count = stream->toc_count;
    xx_ap4_stream_free(stream);
    return true;
}

int64_t xx_ap4_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ap4_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ap4 *)self)->number_of_records : 0U;
}

/* -------------------------------------------------------------- records - */

static bool xx_ap4_set_record(xx_archive_record *record,
                              const xx_ap4_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->offset;
    record->header_size = 0;
    record->data_offset = member->offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           /* XOR obfuscation with a recovered key is not encryption: nothing
            * is withheld from the reader, so extraction never needs a
            * password and the flag would mislead a caller that checks it. */
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ap4_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ap4_get_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_ap4_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ap4_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ap4_parse(self, false, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ap4_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ap4_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ap4_copy_options(&state->options, options) ||
        !xx_ap4_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ap4_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ap4_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_ap4_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ap4_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_ap4_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

/*
 * Copy one member out, undoing the XOR key when the member carries one.
 */
static bool xx_ap4_write_member(Abstractformat *self,
                                const xx_ap4_member *member,
                                xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t position = member->offset;
    int64_t end = member->offset + member->size;
    bool result = true;

    buffer = (uint8_t *)xx_mem_alloc(XX_AP4_COPY_BUFFER_SIZE);
    if (!buffer) return false;
    while (position < end) {
        size_t take = (size_t)(end - position < XX_AP4_COPY_BUFFER_SIZE
                                   ? end - position
                                   : XX_AP4_COPY_BUFFER_SIZE);
        size_t written = 0U;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_ap4_read_at(self, position, buffer, take)) {
            result = false;
            break;
        }
        if (member->xor_key != 0U) {
            size_t i;
            for (i = 0U; i < take; ++i) {
                buffer[i] = (uint8_t)(buffer[i] ^ member->xor_key);
            }
        }
        while (written < take) {
            ssize_t sent =
                xx_io_write(destination, buffer + written, take - written);
            if (sent <= 0 || (size_t)sent > take - written) {
                result = false;
                break;
            }
            written += (size_t)sent;
        }
        if (!result) break;
        position += (int64_t)take;
    }
    xx_mem_free(buffer);
    return result;
}

bool xx_ap4_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_ap4_stream *stream;
    const xx_ap4_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ap4_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];

    path_option = xx_ap4_get_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: verify the member is readable end to end, which is
         * what a caller iterating records without extracting is asking. */
        uint8_t *buffer = (uint8_t *)xx_mem_alloc(XX_AP4_COPY_BUFFER_SIZE);
        int64_t position = member->offset;
        int64_t end = member->offset + member->size;
        result = buffer != NULL;
        while (result && position < end) {
            size_t take = (size_t)(end - position < XX_AP4_COPY_BUFFER_SIZE
                                       ? end - position
                                       : XX_AP4_COPY_BUFFER_SIZE);
            result = !(pd && xx_pd_is_stopped(pd)) &&
                     xx_ap4_read_at(self, position, buffer, take);
            position += (int64_t)take;
        }
        xx_mem_free(buffer);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path || !member->name) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path || !xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        result = output && xx_ap4_write_member(self, member, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_ap4_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

int64_t xx_ap4_get_prefix_size(const xx_ap4 *archive) {
    return archive ? archive->prefix_size : 0;
}

uint32_t xx_ap4_get_toc_count(const xx_ap4 *archive) {
    return archive ? archive->toc_count : 0U;
}
