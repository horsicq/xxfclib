/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the HxC Floppy Emulator HFE v3 image ("HXCHFEV3").
 *
 * Layout (written from the published HFE v3 description, no code taken from
 * HxC): a 512-byte header, a track lookup table of (block, byte length)
 * pairs, and per cylinder a run of 512-byte blocks in which the two sides
 * alternate in 256-byte halves.  Each byte holds eight bit cells, first cell
 * in the least significant bit.  Unlike v1 the cell stream carries opcodes:
 * a byte whose bit-reversed value is 0xF0..0xFF is not cell data.
 *   0xF0 NOP          1 byte, no cells
 *   0xF1 SETINDEX     1 byte, no cells (index pulse position)
 *   0xF2 SETBITRATE   2 bytes, no cells
 *   0xF3 SKIPBITS     3 bytes: count n, then a byte of which the first n
 *                     cells are dropped
 *   0xF4 RAND         1 byte standing for 8 weak (random) cells
 * Such bytes can never be MFM cell data (four adjacent flux cells).
 *
 * The member is the decoded flat sector image: IBM MFM (A1 sync, ID and data
 * fields with CRC-16) or Amiga MFM (4489 4489 sync, odd/even longwords with
 * XOR checksums), placed by physical cylinder, side and sector number.  When
 * neither layout is found the reader exposes one raw cell stream per track
 * side instead, so the container is never silently empty.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/hxc_hfe_v3/xx_hxc_hfe_v3.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef HXC_HFE_V3
#define XX_HXC_HFE_V3_FILE_TYPE XX_FILE_TYPE_HXC_HFE_V3
#else
#define XX_HXC_HFE_V3_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define V3_HEADER_SIZE 512
#define V3_BLOCK 512
#define V3_HALF 256
#define V3_MAX_TRACK_BYTES 65535U
#define V3_SIDE_BYTES 32768U               /* 128 blocks x 256 bytes */
#define V3_CELL_CAP (V3_SIDE_BYTES * 8U)
#define V3_MAX_SYNCS 32768U
#define V3_SYNC 0x4489U
#define V3_MAX_OUTPUT (64U * 1024U * 1024U)
#define V3_FIELD_CAP (16384U + 2U)
#define V3_AMIGA_CELLS 8640U               /* info .. end of data, after sync */

#define V3_MODE_RAW 0
#define V3_MODE_IBM 1
#define V3_MODE_AMIGA 2

typedef struct v3_header_s {
    int32_t tracks;
    int32_t sides;
    uint8_t encoding;
    int64_t format_size;
    uint32_t offset[256];
    uint32_t length[256];
} v3_header;

typedef struct v3_member_s {
    char *name;
    uint64_t unpacked_size;
    int32_t track;          /* -1 for the flat image */
    int32_t side;
} v3_member;

/* The flat image follows the reference converter's raw export: per track
 * side, the sectors found in ascending ID order, each at its own size. */
typedef struct v3_geometry_s {
    int32_t mode;
    uint32_t slots;
    uint64_t image_size;
    uint8_t *codes;         /* 512 x 256: size code + 1 per ID, 0 = absent */
    uint64_t offset[512];   /* per track * 2 + side */
    uint32_t slot[512];
} v3_geometry;

typedef struct v3_stream_s {
    v3_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    v3_header header;
    v3_geometry geometry;
} v3_stream;

typedef struct v3_sink_s {
    uint8_t *ibm;           /* probe tables, same shape as v3_geometry.codes */
    uint8_t *amiga;
    /* write pass */
    const v3_geometry *geometry;
    uint8_t *image;
    uint8_t *state;         /* per slot: 0 empty, 1 bad CRC, 2 good */
} v3_sink;

typedef struct v3_work_s {
    uint8_t *track;
    uint8_t *half[2];
    size_t half_size[2];
    uint8_t *cells;
    uint8_t *fm;
    uint32_t *syncs;
    uint8_t *field;
} v3_work;

static uint16_t v3_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint8_t v3_rev8(uint8_t v) {
    v = (uint8_t)(((v & 0xF0U) >> 4U) | ((v & 0x0FU) << 4U));
    v = (uint8_t)(((v & 0xCCU) >> 2U) | ((v & 0x33U) << 2U));
    v = (uint8_t)(((v & 0xAAU) >> 1U) | ((v & 0x55U) << 1U));
    return v;
}

static bool v3_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Names are built here, never taken from the container. */
static char *v3_make_name(int32_t track, int32_t side, const char *suffix) {
    char buffer[40];
    size_t used = 0U;
    size_t index;
    char *result;
    const char *prefix = track < 0 ? "image" : "track_";
    for (index = 0U; prefix[index]; ++index) buffer[used++] = prefix[index];
    if (track >= 0) {
        buffer[used++] = (char)('0' + (track / 100) % 10);
        buffer[used++] = (char)('0' + (track / 10) % 10);
        buffer[used++] = (char)('0' + track % 10);
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + side % 10);
    }
    for (index = 0U; suffix[index] && used < sizeof(buffer) - 1U; ++index)
        buffer[used++] = suffix[index];
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, buffer, used + 1U);
    return result;
}

static void v3_stream_free(void *opaque) {
    v3_stream *stream = (v3_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->geometry.codes) xx_mem_free(stream->geometry.codes);
    xx_mem_free(stream);
}

/* Header and lookup table.  Every non-empty track must lie inside the
 * device; the format ends at the last block any track touches. */
static bool v3_read_header(xx_io_device *device, int64_t base,
                           v3_header *out) {
    uint8_t head[V3_HEADER_SIZE];
    uint8_t lut[1024];
    int64_t total, available, lut_offset, end;
    int32_t track;
    bool any = false;
    if (!device || base < 0) return false;
    total = xx_io_total_size(device);
    if (total < base) return false;
    available = total - base;
    if (available < V3_HEADER_SIZE ||
        !v3_read_at(device, base, head, sizeof(head)))
        return false;
    if (xx_rt_memcmp(head, "HXCHFEV3", 8U) != 0 || head[8] != 0U ||
        head[9] == 0U || (head[10] != 1U && head[10] != 2U))
        return false;
    xx_rt_memset(out, 0, sizeof(*out));
    out->tracks = (int32_t)head[9];
    out->sides = (int32_t)head[10];
    out->encoding = head[11];
    lut_offset = (int64_t)v3_le16(head + 0x12) * V3_BLOCK;
    if (lut_offset < V3_HEADER_SIZE ||
        lut_offset > available - (int64_t)out->tracks * 4)
        return false;
    if (!v3_read_at(device, base + lut_offset, lut,
                    (size_t)out->tracks * 4U))
        return false;
    end = lut_offset + (int64_t)out->tracks * 4;
    for (track = 0; track < out->tracks; ++track) {
        const int64_t offset =
            (int64_t)v3_le16(lut + (size_t)track * 4U) * V3_BLOCK;
        const int64_t length = (int64_t)v3_le16(lut + (size_t)track * 4U + 2U);
        int64_t stop;
        if (length == 0) continue;
        if (offset < V3_HEADER_SIZE || offset > available ||
            length > available - offset)
            return false;
        out->offset[track] = (uint32_t)offset;
        out->length[track] = (uint32_t)length;
        stop = offset + ((length + V3_BLOCK - 1) / V3_BLOCK) * V3_BLOCK;
        if (stop > available) stop = available;
        if (stop > end) end = stop;
        any = true;
    }
    if (!any) return false;
    out->format_size = end;
    return true;
}

static void v3_work_free(v3_work *work) {
    if (work->track) xx_mem_free(work->track);
    if (work->half[0]) xx_mem_free(work->half[0]);
    if (work->half[1]) xx_mem_free(work->half[1]);
    if (work->cells) xx_mem_free(work->cells);
    if (work->fm) xx_mem_free(work->fm);
    if (work->syncs) xx_mem_free(work->syncs);
    if (work->field) xx_mem_free(work->field);
    xx_rt_memset(work, 0, sizeof(*work));
}

static bool v3_work_alloc(v3_work *work) {
    xx_rt_memset(work, 0, sizeof(*work));
    work->track = (uint8_t *)xx_mem_alloc(V3_MAX_TRACK_BYTES + 1U);
    work->half[0] = (uint8_t *)xx_mem_alloc(V3_SIDE_BYTES);
    work->half[1] = (uint8_t *)xx_mem_alloc(V3_SIDE_BYTES);
    work->cells = (uint8_t *)xx_mem_alloc(V3_CELL_CAP);
    work->fm = (uint8_t *)xx_mem_alloc(V3_CELL_CAP / 2U + 1U);
    work->syncs = (uint32_t *)xx_mem_alloc(V3_MAX_SYNCS * sizeof(uint32_t));
    work->field = (uint8_t *)xx_mem_alloc(V3_FIELD_CAP);
    if (!work->track || !work->half[0] || !work->half[1] || !work->cells ||
        !work->fm || !work->syncs || !work->field) {
        v3_work_free(work);
        return false;
    }
    return true;
}

/* Read one cylinder and split it into the two side halves. */
static bool v3_load_track(xx_io_device *device, int64_t base,
                          const v3_header *header, int32_t track,
                          v3_work *work) {
    const uint32_t length = header->length[track];
    uint32_t chunk;
    work->half_size[0] = 0U;
    work->half_size[1] = 0U;
    if (length == 0U) return true;
    if (length > V3_MAX_TRACK_BYTES ||
        !v3_read_at(device, base + (int64_t)header->offset[track],
                    work->track, length))
        return false;
    for (chunk = 0U; chunk < length; chunk += V3_BLOCK) {
        int32_t side;
        for (side = 0; side < 2; ++side) {
            uint32_t start = chunk + (uint32_t)side * V3_HALF;
            uint32_t stop = start + V3_HALF;
            if (start >= length) continue;
            if (stop > length) stop = length;
            if (work->half_size[side] + (stop - start) > V3_SIDE_BYTES)
                return false;
            xx_rt_memcpy(work->half[side] + work->half_size[side],
                         work->track + start, stop - start);
            work->half_size[side] += stop - start;
        }
    }
    return true;
}

/* Turn one side's bytes into cells (one byte per cell), consuming opcodes.
 * Every input byte yields at most eight cells, so the output fits. */
static size_t v3_expand(const uint8_t *in, size_t size, uint8_t *cells) {
    size_t i = 0U, used = 0U;
    while (i < size) {
        const uint8_t value = v3_rev8(in[i]);
        int32_t bit;
        if ((value & 0xF0U) == 0xF0U) {
            switch (value) {
            case 0xF2U:
                i += 2U;
                break;
            case 0xF3U: {
                uint8_t skip, data;
                if (i + 2U >= size) {
                    i = size;
                    break;
                }
                skip = v3_rev8(in[i + 1U]);
                data = v3_rev8(in[i + 2U]);
                if (skip < 8U)
                    for (bit = (int32_t)skip; bit < 8; ++bit)
                        cells[used++] = (uint8_t)((data >> (7 - bit)) & 1U);
                i += 3U;
                break;
            }
            case 0xF4U:
                for (bit = 0; bit < 8; ++bit) cells[used++] = 0U;
                i += 1U;
                break;
            default:            /* NOP, SETINDEX, reserved */
                i += 1U;
                break;
            }
            continue;
        }
        for (bit = 7; bit >= 0; --bit)
            cells[used++] = (uint8_t)((value >> bit) & 1U);
        ++i;
    }
    return used;
}

static uint32_t v3_find_syncs(const uint8_t *cells, size_t count,
                              uint32_t *syncs) {
    uint32_t found = 0U, shift = 0U;
    size_t i;
    for (i = 0U; i < count; ++i) {
        shift = ((shift << 1U) | cells[i]) & 0xFFFFU;
        if (i >= 15U && shift == V3_SYNC) {
            if (found >= V3_MAX_SYNCS) break;
            syncs[found++] = (uint32_t)(i - 15U);
        }
    }
    return found;
}

static uint16_t v3_crc16(const uint8_t *data, size_t size, uint16_t crc) {
    return xx_crc16_ccitt_calc(crc, data, size);
}

/* Tracks are loops: a field that runs past the end of the stream continues
 * at its start (as the reference converter reads it), but a read never
 * spans more than one revolution.  An MFM byte is sixteen cells; the data
 * bits are the odd-indexed ones. */
static bool v3_mfm_bytes(const uint8_t *cells, size_t count, size_t position,
                         size_t size, uint8_t *out) {
    size_t index;
    if (count < 16U || position >= count || size > count / 16U) return false;
    for (index = 0U; index < size; ++index) {
        const size_t at = position + index * 16U + 1U;
        uint32_t value = 0U;
        size_t k;
        if (at + 15U <= count) {
            const uint8_t *bits = cells + at;
            for (k = 0U; k < 8U; ++k) value = (value << 1U) | bits[k * 2U];
        } else {
            for (k = 0U; k < 8U; ++k)
                value = (value << 1U) | cells[(at + k * 2U) % count];
        }
        out[index] = (uint8_t)value;
    }
    return true;
}

static uint32_t v3_raw32(const uint8_t *cells, size_t count, size_t position) {
    uint32_t value = 0U;
    size_t k;
    if (position + 32U <= count) {
        const uint8_t *bits = cells + position;
        for (k = 0U; k < 32U; ++k) value = (value << 1U) | bits[k];
    } else {
        for (k = 0U; k < 32U; ++k)
            value = (value << 1U) | cells[(position + k) % count];
    }
    return value;
}

/* A sector whose ID field checked out.  While probing, the first size seen
 * for each ID of each track side is recorded; in the write pass the sector
 * lands at its rank among that side's IDs.  A good copy always wins over a
 * bad one; a bad copy only fills an empty slot. */
static void v3_sector(v3_sink *sink, int32_t mode, int32_t track, int32_t side,
                      int32_t id, int32_t size_code, const uint8_t *data,
                      bool data_ok) {
    size_t ts;
    if (track < 0 || track > 255 || side < 0 || side > 1 || id < 0 ||
        id > 255 || size_code < 0 || size_code > 7)
        return;
    ts = (size_t)track * 2U + (size_t)side;
    if (!sink->image) {
        uint8_t *codes = mode == V3_MODE_AMIGA ? sink->amiga : sink->ibm;
        if (codes && codes[ts * 256U + (size_t)id] == 0U)
            codes[ts * 256U + (size_t)id] = (uint8_t)(size_code + 1);
        return;
    }
    {
        const v3_geometry *g = sink->geometry;
        const uint8_t *codes = g->codes + ts * 256U;
        const uint64_t size = (uint64_t)128U << (uint32_t)size_code;
        uint64_t slot, position;
        int32_t k;
        if (mode != g->mode || codes[id] != (uint8_t)(size_code + 1)) return;
        slot = g->slot[ts];
        position = g->offset[ts];
        for (k = 0; k < id; ++k)
            if (codes[k] != 0U) {
                ++slot;
                position += (uint64_t)128U << (uint32_t)(codes[k] - 1U);
            }
        if (slot >= g->slots || position > g->image_size ||
            size > g->image_size - position)
            return;
        if (sink->state[slot] == 2U || (!data_ok && sink->state[slot] != 0U))
            return;
        xx_rt_memcpy(sink->image + position, data, (size_t)size);
        sink->state[slot] = data_ok ? 2U : 1U;
    }
}

static void v3_decode_ibm(const uint8_t *cells, size_t count,
                          const uint32_t *syncs, uint32_t sync_count,
                          int32_t track, int32_t side, uint8_t *field,
                          v3_sink *sink) {
    uint32_t s;
    bool have_id = false;
    int32_t id_sector = 0, id_size = 0;
    for (s = 0U; s + 2U < sync_count;) {
        uint8_t mark;
        size_t position;
        if (syncs[s + 1U] != syncs[s] + 16U ||
            syncs[s + 2U] != syncs[s] + 32U) {
            ++s;
            continue;
        }
        position = ((size_t)syncs[s] + 48U) % (count ? count : 1U);
        if (!v3_mfm_bytes(cells, count, position, 1U, &mark)) break;
        position = (position + 16U) % count;
        if (mark == 0xFEU) {
            have_id = false;
            if (v3_mfm_bytes(cells, count, position, 6U, field)) {
                uint8_t preamble[4] = {0xA1U, 0xA1U, 0xA1U, 0xFEU};
                uint16_t crc = v3_crc16(preamble, 4U, 0xFFFFU);
                crc = v3_crc16(field, 4U, crc);
                if (crc == (uint16_t)(((uint16_t)field[4] << 8U) | field[5])) {
                    id_sector = field[2];
                    id_size = field[3] & 7;
                    have_id = true;
                }
            }
        } else if (mark >= 0xF8U && mark <= 0xFBU) {
            if (have_id) {
                const size_t size = (size_t)128U << (uint32_t)id_size;
                /* Syncs inside the data field are data, not new fields; this
                 * also keeps the work per side linear in its length. */
                const size_t end = (size_t)syncs[s] + 64U + (size + 2U) * 16U;
                have_id = false;
                if (v3_mfm_bytes(cells, count, position, size + 2U, field)) {
                    uint8_t preamble[4] = {0xA1U, 0xA1U, 0xA1U, 0};
                    uint16_t crc;
                    preamble[3] = mark;
                    crc = v3_crc16(preamble, 4U, 0xFFFFU);
                    crc = v3_crc16(field, size, crc);
                    v3_sector(sink, V3_MODE_IBM, track, side, id_sector,
                              id_size, field,
                              crc == (uint16_t)(((uint16_t)field[size] << 8U) |
                                                field[size + 1U]));
                }
                s += 3U;
                while (s < sync_count && syncs[s] < end) ++s;
                continue;
            }
        }
        s += 3U;
    }
}

/* Amiga sector after the two sync words: info, label, header checksum and
 * data checksum as odd/even longword pairs, then 128 odd and 128 even data
 * longwords.  Checksums XOR the raw longwords masked to their data bits. */
static void v3_decode_amiga(const uint8_t *cells, size_t count,
                            const uint32_t *syncs, uint32_t sync_count,
                            int32_t track, int32_t side, uint8_t *field,
                            v3_sink *sink) {
    const uint32_t mask = 0x55555555U;
    uint32_t s;
    if (count < V3_AMIGA_CELLS) return;
    for (s = 0U; s + 1U < sync_count;) {
        size_t q;
        uint32_t info, check, stored, k;
        if (syncs[s + 1U] != syncs[s] + 16U) {
            ++s;
            continue;
        }
        q = (size_t)syncs[s] + 32U;
        info = ((v3_raw32(cells, count, q) & mask) << 1U) |
               (v3_raw32(cells, count, q + 32U) & mask);
        if ((info >> 24U) != 0xFFU) {
            ++s;
            continue;
        }
        check = 0U;
        for (k = 0U; k < 10U; ++k)
            check ^= v3_raw32(cells, count, q + k * 32U);
        check &= mask;
        stored = ((v3_raw32(cells, count, q + 320U) & mask) << 1U) |
                 (v3_raw32(cells, count, q + 352U) & mask);
        if (check != stored) {
            ++s;
            continue;
        }
        stored = ((v3_raw32(cells, count, q + 384U) & mask) << 1U) |
                 (v3_raw32(cells, count, q + 416U) & mask);
        check = 0U;
        for (k = 0U; k < 128U; ++k) {
            const uint32_t odd = v3_raw32(cells, count, q + 448U + k * 32U);
            const uint32_t even =
                v3_raw32(cells, count, q + 448U + 4096U + k * 32U);
            const uint32_t value = ((odd & mask) << 1U) | (even & mask);
            check ^= odd ^ even;
            field[k * 4U] = (uint8_t)(value >> 24U);
            field[k * 4U + 1U] = (uint8_t)(value >> 16U);
            field[k * 4U + 2U] = (uint8_t)(value >> 8U);
            field[k * 4U + 3U] = (uint8_t)value;
        }
        v3_sector(sink, V3_MODE_AMIGA, track, side,
                  (int32_t)((info >> 8U) & 0xFFU), 2, field,
                  (check & mask) == stored);
        /* Skip syncs inside the sector just read (linear work per side). */
        s += 2U;
        while (s < sync_count && syncs[s] < q + V3_AMIGA_CELLS) ++s;
    }
}

/* FM keeps the IBM field layout but has no A1 preamble: the address marks
 * carry the clock pattern C7.  An FM cell spans two HFE cells, so the stream
 * is thinned to every other cell, trying both phases. */
static uint16_t v3_fm_mark(uint8_t data) {
    uint16_t value = 0U;
    int32_t bit;
    for (bit = 7; bit >= 0; --bit)
        value = (uint16_t)((value << 2U) | (((0xC7U >> bit) & 1U) << 1U) |
                           ((data >> bit) & 1U));
    return value;
}

static void v3_decode_fm(const uint8_t *cells, size_t count, uint8_t *fm,
                         int32_t track, int32_t side, uint8_t *field,
                         v3_sink *sink) {
    uint16_t marks[5];
    int32_t phase;
    marks[0] = v3_fm_mark(0xFEU);
    marks[1] = v3_fm_mark(0xF8U);
    marks[2] = v3_fm_mark(0xF9U);
    marks[3] = v3_fm_mark(0xFAU);
    marks[4] = v3_fm_mark(0xFBU);
    for (phase = 0; phase < 2; ++phase) {
        size_t n = 0U, i, filled = 0U;
        uint32_t shift = 0U;
        bool have_id = false;
        int32_t id_sector = 0, id_size = 0;
        for (i = (size_t)phase; i < count; i += 2U) fm[n++] = cells[i];
        for (i = 0U; i < n; ++i) {
            uint8_t mark = 0U;
            int32_t k;
            shift = ((shift << 1U) | fm[i]) & 0xFFFFU;
            if (++filled < 16U) continue;
            for (k = 0; k < 5; ++k)
                if (shift == marks[k])
                    mark = k == 0 ? 0xFEU : (uint8_t)(0xF7U + k);
            if (mark == 0U) continue;
            if (mark == 0xFEU) {
                uint8_t head = 0xFEU;
                uint16_t crc;
                have_id = false;
                if (!v3_mfm_bytes(fm, n, (i + 1U) % n, 6U, field)) break;
                crc = v3_crc16(field, 4U, v3_crc16(&head, 1U, 0xFFFFU));
                if (crc == (uint16_t)(((uint16_t)field[4] << 8U) | field[5])) {
                    id_sector = field[2];
                    id_size = field[3] & 7;
                    have_id = true;
                }
                i += 6U * 16U;
            } else if (have_id) {
                const size_t size = (size_t)128U << (uint32_t)id_size;
                uint16_t crc;
                have_id = false;
                if (!v3_mfm_bytes(fm, n, (i + 1U) % n, size + 2U, field)) break;
                crc = v3_crc16(field, size, v3_crc16(&mark, 1U, 0xFFFFU));
                v3_sector(sink, V3_MODE_IBM, track, side, id_sector, id_size,
                          field,
                          crc == (uint16_t)(((uint16_t)field[size] << 8U) |
                                            field[size + 1U]));
                i += (size + 2U) * 16U;
            }
            filled = 0U;
            shift = 0U;
        }
    }
}

/* Walk every track side.  With cell_counts the expanded length of each
 * side is recorded; with mode set only that family of decoders runs. */
static bool v3_walk(xx_io_device *device, int64_t base,
                    const v3_header *header, int32_t mode, v3_sink *sink,
                    uint32_t (*cell_counts)[2], xx_pd_struct *pd) {
    v3_work work;
    int32_t track;
    bool ok = true;
    if (!v3_work_alloc(&work)) return false;
    for (track = 0; ok && track < header->tracks; ++track) {
        int32_t side;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        if (!v3_load_track(device, base, header, track, &work)) {
            ok = false;
            break;
        }
        for (side = 0; side < header->sides; ++side) {
            const size_t cells =
                v3_expand(work.half[side], work.half_size[side], work.cells);
            uint32_t syncs;
            if (cell_counts) cell_counts[track][side] = (uint32_t)cells;
            syncs = v3_find_syncs(work.cells, cells, work.syncs);
            if (mode != V3_MODE_AMIGA) {
                v3_decode_ibm(work.cells, cells, work.syncs, syncs, track, side,
                              work.field, sink);
                v3_decode_fm(work.cells, cells, work.fm, track, side,
                             work.field, sink);
            }
            if (mode != V3_MODE_IBM)
                v3_decode_amiga(work.cells, cells, work.syncs, syncs, track,
                                side, work.field, sink);
        }
    }
    v3_work_free(&work);
    return ok;
}

static uint32_t v3_count_ids(const uint8_t *codes) {
    uint32_t total = 0U;
    size_t index;
    for (index = 0U; index < 512U * 256U; ++index)
        if (codes[index] != 0U) ++total;
    return total;
}

/* Pick the decoder family that found more sectors and lay its sectors out;
 * the chosen probe table moves into the geometry. */
static void v3_choose(const v3_header *header, v3_sink *sink,
                      v3_geometry *out) {
    const uint32_t ibm = v3_count_ids(sink->ibm);
    const uint32_t amiga = v3_count_ids(sink->amiga);
    uint64_t size = 0U;
    uint32_t slots = 0U;
    size_t ts, id;
    xx_rt_memset(out, 0, sizeof(*out));
    out->mode = V3_MODE_RAW;
    if (ibm == 0U && amiga == 0U) return;
    if (amiga > ibm || (amiga == ibm && header->encoding == 1U)) {
        out->mode = V3_MODE_AMIGA;
        out->codes = sink->amiga;
        sink->amiga = NULL;
    } else {
        out->mode = V3_MODE_IBM;
        out->codes = sink->ibm;
        sink->ibm = NULL;
    }
    for (ts = 0U; ts < 512U; ++ts) {
        out->offset[ts] = size;
        out->slot[ts] = slots;
        for (id = 0U; id < 256U; ++id) {
            const uint8_t code = out->codes[ts * 256U + id];
            if (code == 0U) continue;
            ++slots;
            size += (uint64_t)128U << (uint32_t)(code - 1U);
        }
        if (size > V3_MAX_OUTPUT) {
            xx_mem_free(out->codes);
            out->codes = NULL;
            out->mode = V3_MODE_RAW;
            return;
        }
    }
    out->slots = slots;
    out->image_size = size;
}

static bool v3_add_member(v3_stream *stream, int32_t track, int32_t side,
                          uint64_t size, const char *suffix) {
    v3_member *grown;
    v3_member member;
    if (stream->count >= 512U) return false;
    member.name = v3_make_name(track, side, suffix);
    if (!member.name) return false;
    member.unpacked_size = size;
    member.track = track;
    member.side = side;
    grown = (v3_member *)xx_mem_realloc(stream->items,
                                        (stream->count + 1U) * sizeof(*grown));
    if (!grown) {
        xx_mem_free(member.name);
        return false;
    }
    stream->items = grown;
    stream->items[stream->count++] = member;
    return true;
}

static bool v3_parse(Abstractformat *format, bool full, v3_stream **result,
                     xx_pd_struct *pd) {
    v3_stream *stream;
    v3_sink sink;
    uint32_t (*cell_counts)[2] = NULL;
    bool ok;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    stream = (v3_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!v3_read_header(format->device, format->base_address,
                        &stream->header)) {
        v3_stream_free(stream);
        return false;
    }
    stream->archive_size = stream->header.format_size;
    if (!full) {
        *result = stream;
        return true;
    }
    cell_counts = (uint32_t (*)[2])xx_mem_calloc(256U, sizeof(*cell_counts));
    if (!cell_counts) {
        v3_stream_free(stream);
        return false;
    }
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.ibm = (uint8_t *)xx_mem_calloc(512U * 256U, 1U);
    sink.amiga = (uint8_t *)xx_mem_calloc(512U * 256U, 1U);
    ok = sink.ibm && sink.amiga &&
         v3_walk(format->device, format->base_address, &stream->header,
                 V3_MODE_RAW, &sink, cell_counts, pd);
    if (ok) v3_choose(&stream->header, &sink, &stream->geometry);
    if (ok) {
        if (stream->geometry.mode != V3_MODE_RAW) {
            ok = v3_add_member(stream, -1, 0, stream->geometry.image_size,
                               stream->geometry.mode == V3_MODE_AMIGA ? ".adf"
                                                                      : ".img");
        } else {
            int32_t track, side;
            for (track = 0; ok && track < stream->header.tracks; ++track)
                for (side = 0; ok && side < stream->header.sides; ++side)
                    ok = v3_add_member(stream, track, side,
                                       ((uint64_t)cell_counts[track][side] + 7U) /
                                           8U,
                                       ".bin");
        }
    }
    xx_mem_free(cell_counts);
    if (sink.ibm) xx_mem_free(sink.ibm);
    if (sink.amiga) xx_mem_free(sink.amiga);
    if (!ok || stream->count == 0U) {
        v3_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool v3_extract_image(Abstractformat *format, const v3_stream *stream,
                             uint8_t **plain, size_t *plain_size,
                             xx_pd_struct *pd) {
    const v3_geometry *g = &stream->geometry;
    const uint64_t slots = g->slots;
    v3_sink sink;
    uint8_t *image, *state;
    if (!g->codes || g->image_size == 0U || g->image_size > V3_MAX_OUTPUT)
        return false;
    image = (uint8_t *)xx_mem_alloc((size_t)g->image_size);
    state = (uint8_t *)xx_mem_alloc((size_t)slots + 1U);
    if (!image || !state) {
        if (image) xx_mem_free(image);
        if (state) xx_mem_free(state);
        return false;
    }
    /* Missing sectors stay zero so the image keeps its geometry. */
    xx_rt_memset(image, 0, (size_t)g->image_size);
    xx_rt_memset(state, 0, (size_t)slots + 1U);
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.geometry = g;
    sink.image = image;
    sink.state = state;
    if (!v3_walk(format->device, format->base_address, &stream->header,
                 g->mode, &sink, NULL, pd)) {
        xx_mem_free(image);
        xx_mem_free(state);
        return false;
    }
    xx_mem_free(state);
    *plain = image;
    *plain_size = (size_t)g->image_size;
    return true;
}

static bool v3_extract_track(Abstractformat *format, const v3_stream *stream,
                             const v3_member *member, uint8_t **plain,
                             size_t *plain_size) {
    v3_work work;
    uint8_t *output;
    size_t cells, index;
    if (!v3_work_alloc(&work)) return false;
    if (!v3_load_track(format->device, format->base_address, &stream->header,
                       member->track, &work)) {
        v3_work_free(&work);
        return false;
    }
    cells = v3_expand(work.half[member->side], work.half_size[member->side],
                      work.cells);
    if ((uint64_t)(cells + 7U) / 8U != member->unpacked_size) {
        v3_work_free(&work);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc(cells / 8U + 1U);
    if (!output) {
        v3_work_free(&work);
        return false;
    }
    xx_rt_memset(output, 0, cells / 8U + 1U);
    for (index = 0U; index < cells; ++index)
        if (work.cells[index])
            output[index / 8U] |= (uint8_t)(0x80U >> (index % 8U));
    v3_work_free(&work);
    *plain = output;
    *plain_size = (cells + 7U) / 8U;
    return true;
}

static bool v3_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *v3_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool v3_set_record(xx_archive_record *record, const v3_stream *stream,
                          const v3_member *member, int64_t base) {
    int64_t offset = base, size = stream->archive_size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (member->track >= 0) {
        offset = base + (int64_t)stream->header.offset[member->track];
        size = (int64_t)stream->header.length[member->track];
    }
    record->header_offset = base;
    record->header_size = V3_HEADER_SIZE;
    record->data_offset = offset;
    record->compressed_size = size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_hxc_hfe_v3_init(xx_hxc_hfe_v3 *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HXC_HFE_V3_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hfe");
    xx_format_set_extension(&archive->format, "hfe");
    archive->format.check_is_valid = xx_hxc_hfe_v3_check_is_valid;
    archive->format.handle_base_info = xx_hxc_hfe_v3_handle_base_info;
    archive->format.get_format_size = xx_hxc_hfe_v3_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hxc_hfe_v3_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hxc_hfe_v3_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hxc_hfe_v3_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hxc_hfe_v3_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hxc_hfe_v3_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hxc_hfe_v3_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_hxc_hfe_v3 *xx_hxc_hfe_v3_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_hxc_hfe_v3 *archive =
        (xx_hxc_hfe_v3 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hxc_hfe_v3_init(archive, device, base_address);
    return archive;
}

void xx_hxc_hfe_v3_destroy(xx_hxc_hfe_v3 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_hxc_hfe_v3_free(xx_hxc_hfe_v3 *archive) {
    if (!archive) return;
    xx_hxc_hfe_v3_destroy(archive);
    xx_mem_free(archive);
}

/* Header and lookup table only: cheap, bounded, no track decoding. */
bool xx_hxc_hfe_v3_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    v3_stream *stream;
    (void)pd;
    if (!v3_parse(format, false, &stream, NULL)) return false;
    v3_stream_free(stream);
    return true;
}

bool xx_hxc_hfe_v3_handle_base_info(Abstractformat *format,
                                    xx_pd_struct *pd) {
    v3_stream *stream;
    xx_hxc_hfe_v3 *archive;
    if (!format || !v3_parse(format, true, &stream, pd)) return false;
    archive = (xx_hxc_hfe_v3 *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    v3_stream_free(stream);
    return true;
}

int64_t xx_hxc_hfe_v3_get_format_size(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxc_hfe_v3_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_hxc_hfe_v3_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hxc_hfe_v3_handle_base_info(format, pd))
               ? ((xx_hxc_hfe_v3 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_hxc_hfe_v3_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    v3_stream *stream;
    xx_archive_record_state *state;
    if (!v3_parse(format, true, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        v3_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = v3_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!v3_copy_options(&state->options, options) ||
        !v3_set_record(&state->current_record, stream, &stream->items[0],
                       format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_hxc_hfe_v3_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_hxc_hfe_v3_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    v3_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (v3_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = v3_set_record(&state->current_record, stream,
                                      &stream->items[stream->index],
                                      format->base_address);
    return state->has_record;
}

bool xx_hxc_hfe_v3_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    v3_stream *stream;
    v3_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    bool extracted;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (v3_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    extracted = member->track < 0
                    ? v3_extract_image(format, stream, &plain, &plain_size, pd)
                    : v3_extract_track(format, stream, member, &plain,
                                       &plain_size);
    if (!extracted) goto done;
    path_option = v3_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_hxc_hfe_v3_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
