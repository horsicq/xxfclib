/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the HxC Floppy Emulator (HFE v1) image.  The container
 * stores raw bit cells, so members are produced by decoding IBM MFM/FM or
 * AmigaDOS MFM sectors, including their header and data checksums.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/hfe/xx_hfe.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef HFE
#define XX_HFE_FILE_TYPE XX_FILE_TYPE_HFE
#else
#define XX_HFE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HFE_MAX_MEMBERS 65536U
#define HFE_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct hfe_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t method;      /* 0 = stored, non-zero = format codec */
    bool decode;
} hfe_member;

typedef struct hfe_stream_s {
    hfe_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t recovered_cylinders;
    bool incomplete_tracks;
} hfe_stream;

static uint16_t hfe_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t hfe_le32(const uint8_t *b) {
    return (uint32_t)hfe_le16(b) | ((uint32_t)hfe_le16(b + 2U) << 16U);
}

static bool hfe_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    int64_t original;
    bool ok = true;
    if (!device || (!buffer && size != 0U) || offset < 0)
        return false;
    original = xx_io_tell(device);
    if (original < 0) return false;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) {
        (void)xx_io_seek64(device, original, SEEK_SET);
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            ok = false;
            break;
        }
        done += (size_t)amount;
    }
    return xx_io_seek64(device, original, SEEK_SET) == 0 && ok;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction.  The helper only has to be CRT free. */
static char *hfe_make_name(const char *prefix, int a, int b,
                           const char *suffix) {
    char buffer[64];
    size_t used = 0U;
    size_t index;
    char *result;
    for (index = 0U; prefix && prefix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[index];
    }
    if (a >= 0) {
        char digits[8];
        size_t count = 0U;
        int value = a;
        do {
            digits[count++] = (char)('0' + (value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 2U) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    if (b >= 0) {
        if (used >= sizeof(buffer) - 2U) return NULL;
        buffer[used++] = '_';
        buffer[used++] = (char)('0' + (b % 10));
    }
    for (index = 0U; suffix && suffix[index]; ++index) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[index];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

static void hfe_stream_free(void *opaque) {
    hfe_stream *stream = (hfe_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool hfe_add_member(hfe_stream *stream, const hfe_member *member) {
    hfe_member *grown;
    if (!stream || !member || stream->count >= HFE_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (hfe_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define HFE_HEADER_SIZE 512
#define HFE_BLOCK 512
#define HFE_CHUNK 256
#define HFE_MAX_TRACKS 256
#define HFE_MAX_SECTORS 256
#define HFE_MAX_SYNCS 65536U
#define HFE_SYNC_A1 0x4489U

typedef struct hfe_geometry_s {
    int32_t tracks;
    int32_t sides;
    int32_t sectors_per_track;
    int32_t sector_size;
    int32_t first_sector;
    int64_t lut_offset;
    uint64_t image_size;
    bool fm;
    bool amiga;
    bool variable;
    bool incomplete_tracks;
    uint32_t recovered_cylinders;
} hfe_geometry;

/* "HXCPICFE", a zero revision byte, the track and side counts, then a lookup
 * table whose 512-byte-block offset lives at +0x12.  Each LUT entry is a
 * block offset and a byte length for one cylinder; inside it the two sides
 * alternate in 256-byte chunks. */
static bool hfe_header(const uint8_t *file, size_t size, hfe_geometry *out) {
    int64_t lut_size;
    if (size < HFE_HEADER_SIZE) return false;
    if (file[0] != 'H' || file[1] != 'X' || file[2] != 'C' || file[3] != 'P' ||
        file[4] != 'I' || file[5] != 'C' || file[6] != 'F' || file[7] != 'E')
        return false;
    if (file[8] != 0U || file[9] == 0U || (file[10] != 1U && file[10] != 2U))
        return false;
    out->tracks = (int32_t)file[9];
    out->sides = (int32_t)file[10];
    out->sectors_per_track = 0;
    out->sector_size = 0;
    out->first_sector = 0;
    out->fm = file[11] == 2U; /* ISO/IBM FM, doubled in HFE bitcells. */
    out->amiga = file[11] == 1U;
    out->variable = false;
    out->incomplete_tracks = false;
    out->recovered_cylinders = 0U;
    out->lut_offset = (int64_t)hfe_le16(file + 0x12) * HFE_BLOCK;
    if (out->tracks > HFE_MAX_TRACKS || out->lut_offset < HFE_HEADER_SIZE ||
        out->lut_offset > (int64_t)size)
        return false;
    lut_size = (int64_t)out->tracks * 4;
    if (lut_size > (int64_t)size - out->lut_offset) return false;
    return true;
}

/* Deinterleave one cylinder into a bit-cell stream per side. */
static bool hfe_track_bits(const uint8_t *file, size_t size,
                           const hfe_geometry *geometry, int32_t track,
                           uint8_t *side0, uint8_t *side1, size_t capacity,
                           size_t *cells0, size_t *cells1) {
    const int64_t entry = geometry->lut_offset + (int64_t)track * 4;
    const int64_t offset = (int64_t)hfe_le16(file + entry) * HFE_BLOCK;
    const int64_t length = (int64_t)hfe_le16(file + entry + 2);
    const int64_t side_bytes = length / 2;
    size_t used[2];
    int32_t side;
    used[0] = 0U;
    used[1] = 0U;
    if (length <= 0 || (length & 1) != 0 ||
        offset < HFE_HEADER_SIZE || offset > (int64_t)size ||
        side_bytes > (int64_t)(capacity / 8U))
        return false;
    /* The LUT length is for both sides together, while on disk each
     * 512-byte block holds up to 256 bytes of each side.  The last block is
     * padded independently for each side; do not give its padding to side 0
     * or truncate side 1 when the per-side length is not 256-aligned. */
    for (side = 0; side < 2; ++side) {
        int64_t byte_index;
        uint8_t *target = side ? side1 : side0;
        for (byte_index = 0; byte_index < side_bytes; ++byte_index) {
            int64_t at = offset + (byte_index / HFE_CHUNK) * HFE_BLOCK +
                         (int64_t)side * HFE_CHUNK +
                         byte_index % HFE_CHUNK;
            uint8_t value;
            int32_t bit;
            if (at < 0 || at >= (int64_t)size) return false;
            value = file[at];
            for (bit = 0; bit < 8; ++bit) {
                target[used[side]++] = (uint8_t)((value >> bit) & 1U);
            }
        }
    }
    *cells0 = used[0];
    *cells1 = used[1];
    return used[0] != 0U;
}

static uint16_t hfe_crc16(const uint8_t *data, size_t size, uint16_t crc) {
    return xx_crc16_ccitt_calc(crc, data, size);
}

/* One MFM byte is sixteen cells; the data bits are the odd-indexed ones. */
static bool hfe_mfm_byte(const uint8_t *bits, size_t cells, size_t position,
                         uint8_t *value) {
    uint32_t result = 0U;
    int32_t k;
    if (position + 16U > cells) return false;
    for (k = 0; k < 8; ++k)
        result = (result << 1) | bits[position + (size_t)k * 2U + 1U];
    *value = (uint8_t)result;
    return true;
}

static bool hfe_mfm_bytes(const uint8_t *bits, size_t cells, size_t position,
                          size_t count, uint8_t *out) {
    size_t index;
    for (index = 0U; index < count; ++index)
        if (!hfe_mfm_byte(bits, cells, position + index * 16U, out + index))
            return false;
    return true;
}

#define HFE_AMIGA_MASK 0x55555555U
#define HFE_AMIGA_SYNC 0x44894489U
#define HFE_AMIGA_WORDS 270U
#define HFE_AMIGA_MAX_SECTORS 22U
#define HFE_AMIGA_SECTOR_SIZE 512U

/* A track is a circular bit stream: the index can cut either the sync or
 * sector data. Unlike IBM MFM, AmigaDOS stores all odd data bits followed by
 * all even bits and uses XOR checksums over the masked encoded words.
 * Primary layout: keirf/flashfloppy src/image/adf.c, adf_write_track(). */
static uint32_t hfe_amiga_word(const uint8_t *bits, size_t cells,
                               size_t position) {
    uint32_t value = 0U;
    unsigned bit;
    position %= cells;
    for (bit = 0U; bit < 32U; ++bit) {
        value = (value << 1U) | bits[position];
        if (++position == cells) position = 0U;
    }
    return value;
}

static bool hfe_amiga_side(const uint8_t *bits, size_t cells,
                            uint32_t track, uint8_t *sectors,
                            uint32_t *count, xx_pd_struct *pd) {
    uint32_t words[HFE_AMIGA_WORDS];
    uint8_t payload[HFE_AMIGA_SECTOR_SIZE];
    uint8_t seen[HFE_AMIGA_MAX_SECTORS];
    uint32_t shift = 0U, found = 0U, max_remaining = 0U;
    size_t position;
    xx_mem_zero(seen, sizeof(seen));
    *count = 0U;
    if (cells < (HFE_AMIGA_WORDS + 1U) * 32U || track > 255U)
        return false;
    for (position = 0U; position < cells + 31U; ++position) {
        uint32_t info, id, header_sum = 0U, data_sum = 0U, stored;
        size_t word, at;
        if ((position & 4095U) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        shift = (shift << 1U) | bits[position < cells ? position : position - cells];
        if (position < 31U || shift != HFE_AMIGA_SYNC) continue;
        at = position + 1U;
        /* False syncs are common on arbitrary raw tracks. Read only the info
         * words before committing to the remaining encoded sector body. */
        for (word = 0U; word < 2U; ++word)
            words[word] = hfe_amiga_word(bits, cells, at + word * 32U);
        info = ((words[0] & HFE_AMIGA_MASK) << 1U) |
               (words[1] & HFE_AMIGA_MASK);
        if ((info >> 24U) != 0xffU) continue; /* Other raw-track formats. */
        for (word = 2U; word < HFE_AMIGA_WORDS; ++word)
            words[word] = hfe_amiga_word(bits, cells, at + word * 32U);
        for (word = 0U; word < 10U; ++word) header_sum ^= words[word];
        stored = ((words[10] & HFE_AMIGA_MASK) << 1U) |
                 (words[11] & HFE_AMIGA_MASK);
        if ((header_sum & HFE_AMIGA_MASK) != stored ||
            ((info >> 16U) & 0xffU) != track)
            return false;
        id = (info >> 8U) & 0xffU;
        if (id >= HFE_AMIGA_MAX_SECTORS || (info & 0xffU) == 0U ||
            (info & 0xffU) > HFE_AMIGA_MAX_SECTORS)
            return false;
        if ((info & 0xffU) > max_remaining) max_remaining = info & 0xffU;
        for (word = 14U; word < HFE_AMIGA_WORDS; ++word)
            data_sum ^= words[word];
        stored = ((words[12] & HFE_AMIGA_MASK) << 1U) |
                 (words[13] & HFE_AMIGA_MASK);
        if ((data_sum & HFE_AMIGA_MASK) != stored) return false;
        for (word = 0U; word < 128U; ++word) {
            uint32_t value = ((words[14U + word] & HFE_AMIGA_MASK) << 1U) |
                             (words[142U + word] & HFE_AMIGA_MASK);
            payload[word * 4U] = (uint8_t)(value >> 24U);
            payload[word * 4U + 1U] = (uint8_t)(value >> 16U);
            payload[word * 4U + 2U] = (uint8_t)(value >> 8U);
            payload[word * 4U + 3U] = (uint8_t)value;
        }
        if (seen[id]) {
            if (xx_mem_compare(sectors + id * HFE_AMIGA_SECTOR_SIZE,
                                payload, sizeof(payload)) != 0)
                return false;
        } else {
            xx_mem_copy(sectors + id * HFE_AMIGA_SECTOR_SIZE,
                        payload, sizeof(payload));
            seen[id] = 1U;
            ++found;
        }
    }
    if (found == 0U) return true;
    if ((found != 11U && found != 22U) || max_remaining > found)
        return false;
    for (position = 0U; position < found; ++position)
        if (!seen[position]) return false;
    *count = found;
    return true;
}

/* Only complete sector tracks are emitted. Undecodable trailing cylinders
 * can follow a complete prefix, but are explicitly partial and never filled
 * with invented zero sectors. Holes, one-sided tracks and damaged checksums
 * reject the conversion, as do decoded tracks after an undecodable cylinder. */
static bool hfe_amiga_walk(const uint8_t *file, size_t size,
                           hfe_geometry *geometry, uint8_t *image,
                           size_t image_capacity, xx_pd_struct *pd) {
    const size_t capacity = 0x40000U;
    uint8_t *bits[2] = {NULL, NULL};
    uint8_t sectors[2][HFE_AMIGA_MAX_SECTORS * HFE_AMIGA_SECTOR_SIZE];
    uint32_t cylinder, expected_count = 0U, recovered = 0U;
    size_t written = 0U;
    bool trailing = false, ok = false;
    if (geometry->sides != 2 || geometry->tracks > 128 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    bits[0] = (uint8_t *)xx_mem_alloc(capacity);
    bits[1] = (uint8_t *)xx_mem_alloc(capacity);
    if (!bits[0] || !bits[1]) goto done;
    for (cylinder = 0U; cylinder < (uint32_t)geometry->tracks; ++cylinder) {
        size_t cells[2] = {0U, 0U};
        uint32_t count[2] = {0U, 0U};
        unsigned side;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!hfe_track_bits(file, size, geometry, (int32_t)cylinder,
                            bits[0], bits[1], capacity, &cells[0], &cells[1]))
            goto done;
        for (side = 0U; side < 2U; ++side)
            if (!hfe_amiga_side(bits[side], cells[side], cylinder * 2U + side,
                                sectors[side], &count[side], pd))
                goto done;
        if (count[0] == 0U && count[1] == 0U) {
            trailing = true;
            continue;
        }
        if (trailing || count[0] == 0U || count[0] != count[1]) goto done;
        if (expected_count == 0U) expected_count = count[0];
        if (count[0] != expected_count) goto done;
        for (side = 0U; side < 2U; ++side) {
            size_t bytes = count[side] * HFE_AMIGA_SECTOR_SIZE;
            if (written > HFE_MAX_OUTPUT - bytes ||
                (image && (written > image_capacity || bytes > image_capacity - written)))
                goto done;
            if (image) xx_mem_copy(image + written, sectors[side], bytes);
            written += bytes;
        }
        ++recovered;
    }
    if (recovered == 0U || (image && written != image_capacity)) goto done;
    geometry->recovered_cylinders = recovered;
    geometry->incomplete_tracks = recovered != (uint32_t)geometry->tracks;
    geometry->sectors_per_track = (int32_t)expected_count;
    geometry->sector_size = HFE_AMIGA_SECTOR_SIZE;
    geometry->first_sector = 0;
    geometry->image_size = written;
    ok = true;
done:
    if (bits[0]) xx_mem_free(bits[0]);
    if (bits[1]) xx_mem_free(bits[1]);
    return ok;
}

/* Variable IBM layouts have different sector counts, IDs, sizes or encodings
 * on individual tracks.  Keep decoded sectors by physical side and logical
 * sector ID, then concatenate them in IMG order (cylinder, side, ID). */
typedef struct hfe_captured_sector_s {
    uint32_t offset;
    uint16_t size;
    bool present;
} hfe_captured_sector;

typedef struct hfe_captured_side_s {
    hfe_captured_sector sectors[HFE_MAX_SECTORS];
    uint32_t count;
    uint32_t first;
    uint32_t last;
    bool saw_id;
    bool fm;
} hfe_captured_side;

typedef struct hfe_capture_s {
    hfe_captured_side *sides;
    uint8_t *bytes;
    size_t used;
    size_t capacity;
    uint64_t image_size;
    bool variable;
    bool size_or_encoding_variable;
} hfe_capture;

static void hfe_capture_free(hfe_capture *capture) {
    if (!capture) return;
    if (capture->sides) xx_mem_free(capture->sides);
    if (capture->bytes) xx_mem_free(capture->bytes);
    xx_mem_zero(capture, sizeof(*capture));
}

static bool hfe_capture_sector(hfe_capture *capture, hfe_captured_side *side,
                               uint32_t id, const uint8_t *data,
                               uint32_t size) {
    hfe_captured_sector *sector;
    size_t needed, capacity;
    uint8_t *grown;
    if (!capture || !side || !data || id >= HFE_MAX_SECTORS ||
        size == 0U || size > 16384U || side->sectors[id].present ||
        capture->used > HFE_MAX_OUTPUT - size)
        return false;
    needed = capture->used + size;
    if (needed > capture->capacity) {
        capacity = capture->capacity ? capture->capacity : 65536U;
        while (capacity < needed) {
            if (capacity >= HFE_MAX_OUTPUT / 2U) {
                capacity = HFE_MAX_OUTPUT;
                break;
            }
            capacity *= 2U;
        }
        if (capacity < needed) return false;
        grown = (uint8_t *)xx_mem_realloc(capture->bytes, capacity);
        if (!grown) return false;
        capture->bytes = grown;
        capture->capacity = capacity;
    }
    sector = &side->sectors[id];
    sector->offset = (uint32_t)capture->used;
    sector->size = (uint16_t)size;
    sector->present = true;
    xx_mem_copy(capture->bytes + capture->used, data, size);
    capture->used = needed;
    ++side->count;
    if (id < side->first) side->first = id;
    if (id > side->last) side->last = id;
    return true;
}

typedef struct hfe_sink_s {
    uint8_t *image;          /* NULL while probing */
    uint64_t image_size;
    int32_t tracks;
    int32_t sides;
    int32_t sectors_per_track;
    int32_t sector_size;
    int32_t first_sector;
    int32_t min_sector;      /* probe output */
    int32_t max_sector;      /* probe output */
    bool mixed;
    hfe_capture *capture;
    hfe_captured_side *capture_side;
} hfe_sink;

/* Decode one physical side of one cylinder.  CHRN head IDs can differ from
 * physical sides (for example, Commodore 1581 reverses them in HFE). */
static bool hfe_decode_side(const uint8_t *bits, size_t cells,
                            uint32_t *syncs, hfe_sink *sink,
                            int32_t track, int32_t side) {
    uint32_t sync_count = 0U;
    uint32_t shift = 0U;
    size_t i;
    uint32_t s;
    bool have_id = false;
    int32_t id_cylinder = 0, id_head = 0, id_sector = 0, id_size = 0;
    bool id_valid = false;
    uint8_t field[16386];
    for (i = 0U; i < cells; ++i) {
        shift = ((shift << 1) | bits[i]) & 0xffffU;
        if (i >= 15U && shift == HFE_SYNC_A1) {
            if (sync_count >= HFE_MAX_SYNCS) break;
            syncs[sync_count++] = (uint32_t)(i - 15U);
        }
    }
    for (s = 0U; s + 2U < sync_count;) {
        size_t mark;
        uint8_t mark_value;
        if (syncs[s + 1U] != syncs[s] + 16U ||
            syncs[s + 2U] != syncs[s] + 32U) {
            ++s;
            continue;
        }
        mark = (size_t)syncs[s] + 48U;
        if (!hfe_mfm_byte(bits, cells, mark, &mark_value)) break;
        if (mark_value == 0xfeU) {
            if (hfe_mfm_bytes(bits, cells, mark + 16U, 6U, field)) {
                uint8_t preamble[4];
                uint16_t crc;
                if (sink->capture && have_id && id_valid) return false;
                preamble[0] = 0xa1U;
                preamble[1] = 0xa1U;
                preamble[2] = 0xa1U;
                preamble[3] = 0xfeU;
                crc = hfe_crc16(preamble, 4U, 0xffffU);
                crc = hfe_crc16(field, 4U, crc);
                id_cylinder = field[0];
                id_head = field[1];
                id_sector = field[2];
                id_size = field[3] & 7;
                id_valid = crc == (uint16_t)(((uint16_t)field[4] << 8U) |
                                             field[5]);
                if (sink->capture && id_valid)
                    sink->capture_side->saw_id = true;
                have_id = true;
            }
        } else if ((mark_value == 0xfbU || mark_value == 0xf8U) && have_id) {
            const int32_t sector_size = 128 << id_size;
            if (hfe_mfm_bytes(bits, cells, mark + 16U,
                              (size_t)sector_size + 2U, field)) {
                uint8_t preamble[4];
                uint16_t crc;
                bool valid;
                preamble[0] = 0xa1U;
                preamble[1] = 0xa1U;
                preamble[2] = 0xa1U;
                preamble[3] = mark_value;
                crc = hfe_crc16(preamble, 4U, 0xffffU);
                crc = hfe_crc16(field, (size_t)sector_size, crc);
                valid = id_valid &&
                        crc == (uint16_t)(((uint16_t)field[sector_size] << 8U) |
                                          field[sector_size + 1]);
                if (sink->capture) {
                    if (id_valid &&
                        (!valid || id_cylinder != track ||
                         id_head < 0 || id_head > 1 ||
                         !hfe_capture_sector(sink->capture,
                                             sink->capture_side,
                                             (uint32_t)id_sector, field,
                                             (uint32_t)sector_size)))
                        return false;
                } else if (id_sector >= 0 && id_sector < HFE_MAX_SECTORS) {
                    if (!sink->image && valid &&
                        id_cylinder >= 0 && id_cylinder < sink->tracks &&
                        id_head >= 0 && id_head < sink->sides) {
                        if (sink->sector_size == 0)
                            sink->sector_size = sector_size;
                        else if (sink->sector_size != sector_size)
                            sink->mixed = true;
                        if (id_sector < sink->min_sector)
                            sink->min_sector = id_sector;
                        if (id_sector > sink->max_sector)
                            sink->max_sector = id_sector;
                    } else if (valid && sector_size == sink->sector_size &&
                               id_cylinder >= 0 && id_cylinder < sink->tracks &&
                               id_head >= 0 && id_head < sink->sides &&
                               id_sector >= sink->first_sector &&
                               id_sector - sink->first_sector <
                                   sink->sectors_per_track) {
                        const uint64_t position =
                            ((((uint64_t)track * (uint64_t)sink->sides) +
                              (uint64_t)side) *
                                 (uint64_t)sink->sectors_per_track +
                             (uint64_t)(id_sector - sink->first_sector)) *
                            (uint64_t)sink->sector_size;
                        if (position + (uint64_t)sink->sector_size <=
                            sink->image_size)
                            xx_mem_copy(sink->image + position, field,
                                        (size_t)sink->sector_size);
                    }
                }
            }
            have_id = false;
        }
        s += 3U;
    }
    return !sink->capture || !have_id || !id_valid;
}

static bool hfe_walk(const uint8_t *file, size_t size,
                     const hfe_geometry *geometry, bool geometry_only,
                     hfe_sink *sink) {
    const size_t capacity = 0x40000U;
    uint8_t *side0 = (uint8_t *)xx_mem_alloc(capacity);
    uint8_t *side1 = (uint8_t *)xx_mem_alloc(capacity);
    uint32_t *syncs = (uint32_t *)xx_mem_alloc(HFE_MAX_SYNCS * sizeof(uint32_t));
    const int32_t limit = geometry_only ? 1 : geometry->tracks;
    int32_t track;
    bool ok = side0 != NULL && side1 != NULL && syncs != NULL;
    for (track = 0; ok && track < limit; ++track) {
        size_t cells0 = 0U, cells1 = 0U;
        int32_t side;
        if (!hfe_track_bits(file, size, geometry, track, side0, side1, capacity,
                            &cells0, &cells1)) {
            ok = false;
            break;
        }
        for (side = 0; side < geometry->sides; ++side)
            if (!hfe_decode_side(side ? side1 : side0,
                                 side ? cells1 : cells0, syncs,
                                 sink, track, side)) {
                ok = false;
                break;
            }
    }
    if (side0) xx_mem_free(side0);
    if (side1) xx_mem_free(side1);
    if (syncs) xx_mem_free(syncs);
    return ok;
}

/* Greaseweazle and HxC double IBM FM bitcells in HFE v1: one physical FM
 * clock/data pair is stored as 0,clock,0,data.  The address marks retain
 * their missing-clock C7 pattern, so their doubled 32-bit patterns uniquely
 * identify ID and data fields even when the sector bytes contain FE/FB. */
static uint32_t hfe_fm_mark_pattern(uint8_t mark) {
    uint32_t value = 0U;
    uint32_t index;
    for (index = 0U; index < 8U; ++index) {
        value = (value << 2U) | ((0xc7U >> (7U - index)) & 1U);
        value = (value << 2U) | ((mark >> (7U - index)) & 1U);
    }
    return value;
}

static bool hfe_fm_byte(const uint8_t *bits, size_t cells, size_t start,
                        uint8_t *value) {
    uint32_t index, result = 0U;
    if (start > cells || cells - start < 32U) return false;
    for (index = 0U; index < 8U; ++index)
        result = (result << 1U) | bits[start + index * 4U + 3U];
    *value = (uint8_t)result;
    return true;
}

static bool hfe_capture_fm_side(const uint8_t *bits, size_t cells,
                                hfe_capture *capture,
                                hfe_captured_side *side,
                                uint32_t track) {
    const uint32_t id_pattern = hfe_fm_mark_pattern(0xfeU);
    const uint32_t dam_pattern = hfe_fm_mark_pattern(0xfbU);
    const uint32_t ddam_pattern = hfe_fm_mark_pattern(0xf8U);
    uint8_t id[6], field[16386];
    uint32_t shift = 0U, sector_id = 0U, sector_size = 0U;
    size_t id_end = 0U, index;
    bool pending = false;
    for (index = 0U; index < cells; ++index) {
        uint32_t j;
        shift = (shift << 1U) | bits[index];
        if (index < 31U) continue;
        if (shift == id_pattern) {
            uint8_t mark = 0xfeU;
            uint16_t crc;
            size_t start = index + 1U;
            if (pending) return false;
            for (j = 0U; j < 6U; ++j)
                if (!hfe_fm_byte(bits, cells, start + (size_t)j * 32U,
                                 id + j))
                    return !side->saw_id;
            crc = hfe_crc16(&mark, 1U, 0xffffU);
            crc = hfe_crc16(id, 4U, crc);
            pending = crc == (uint16_t)(((uint16_t)id[4] << 8U) | id[5]) &&
                      id[3] <= 7U;
            if (pending) {
                side->saw_id = true;
                if (id[0] != track || id[1] > 1U) return false;
                sector_id = id[2];
                sector_size = 128U << id[3];
                id_end = start + 6U * 32U;
            }
        } else if ((shift == dam_pattern || shift == ddam_pattern) && pending) {
            uint8_t mark = shift == dam_pattern ? 0xfbU : 0xf8U;
            uint16_t crc;
            size_t start = index + 1U;
            if (start < id_end || start - id_end > 4000U) continue;
            if (sector_size > sizeof(field) - 2U) return false;
            for (j = 0U; j < sector_size + 2U; ++j)
                if (!hfe_fm_byte(bits, cells, start + (size_t)j * 32U,
                                 field + j))
                    return false;
            crc = hfe_crc16(&mark, 1U, 0xffffU);
            crc = hfe_crc16(field, sector_size, crc);
            if (crc != (uint16_t)(((uint16_t)field[sector_size] << 8U) |
                                  field[sector_size + 1U]) ||
                !hfe_capture_sector(capture, side, sector_id, field,
                                    sector_size))
                return false;
            pending = false;
        }
    }
    return !pending;
}

static bool hfe_fm_side(const uint8_t *bits, size_t cells,
                        hfe_geometry *geometry, uint32_t track, uint32_t side,
                        bool geometry_only, uint8_t *image) {
    const uint32_t id_pattern = hfe_fm_mark_pattern(0xfeU);
    const uint32_t dam_pattern = hfe_fm_mark_pattern(0xfbU);
    const uint32_t ddam_pattern = hfe_fm_mark_pattern(0xf8U);
    uint8_t field[16386];
    uint8_t id[6];
    uint8_t seen[256];
    uint32_t shift = 0U, count = 0U, min_sector = 256U, max_sector = 0U;
    uint32_t id_track = 0U, id_side = 0U, id_sector = 0U, id_n = 0U;
    size_t id_end = 0U, index;
    bool pending = false;
    xx_mem_zero(seen, sizeof(seen));
    for (index = 0U; index < cells; ++index) {
        uint32_t j;
        shift = (shift << 1U) | bits[index];
        if (index < 31U) continue;
        if (shift == id_pattern) {
            uint8_t mark = 0xfeU;
            uint16_t crc;
            size_t start = index + 1U;
            for (j = 0U; j < 6U; ++j)
                if (!hfe_fm_byte(bits, cells, start + (size_t)j * 32U,
                                 id + j))
                    return false;
            crc = hfe_crc16(&mark, 1U, 0xffffU);
            crc = hfe_crc16(id, 4U, crc);
            pending = crc == (uint16_t)(((uint16_t)id[4] << 8U) | id[5]) &&
                      id[3] <= 7U;
            id_track = id[0];
            id_side = id[1];
            id_sector = id[2];
            id_n = id[3];
            id_end = start + 6U * 32U;
        } else if (shift == dam_pattern || shift == ddam_pattern) {
            uint8_t mark = shift == dam_pattern ? 0xfbU : 0xf8U;
            uint32_t length;
            uint16_t crc;
            size_t start = index + 1U;
            if (!pending || start < id_end || start - id_end > 4000U)
                continue;
            length = 128U << id_n;
            if (length > sizeof(field) - 2U) return false;
            for (j = 0U; j < length + 2U; ++j)
                if (!hfe_fm_byte(bits, cells, start + (size_t)j * 32U,
                                 field + j))
                    return false;
            crc = hfe_crc16(&mark, 1U, 0xffffU);
            crc = hfe_crc16(field, length, crc);
            if (crc != (uint16_t)(((uint16_t)field[length] << 8U) |
                                  field[length + 1U]) ||
                id_track != track ||
                (id_side != side && id_side != 0U) || seen[id_sector])
                return false;
            seen[id_sector] = 1U;
            ++count;
            if (id_sector < min_sector) min_sector = id_sector;
            if (id_sector > max_sector) max_sector = id_sector;
            if (geometry_only) {
                if (geometry->sector_size == 0)
                    geometry->sector_size = (int32_t)length;
                else if (geometry->sector_size != (int32_t)length)
                    return false;
            } else {
                uint64_t slot, offset;
                if (length != (uint32_t)geometry->sector_size ||
                    id_sector < (uint32_t)geometry->first_sector ||
                    id_sector - (uint32_t)geometry->first_sector >=
                        (uint32_t)geometry->sectors_per_track)
                    return false;
                slot = ((uint64_t)track * (uint64_t)geometry->sides + side) *
                           (uint64_t)geometry->sectors_per_track +
                       id_sector - (uint32_t)geometry->first_sector;
                offset = slot * (uint64_t)geometry->sector_size;
                if (offset > geometry->image_size ||
                    geometry->image_size - offset < length)
                    return false;
                if (image) xx_mem_copy(image + (size_t)offset, field, length);
            }
            pending = false;
        }
    }
    if (count == 0U || max_sector - min_sector + 1U != count) return false;
    if (geometry_only) {
        geometry->sectors_per_track = (int32_t)count;
        geometry->first_sector = (int32_t)min_sector;
    } else if (count != (uint32_t)geometry->sectors_per_track ||
               min_sector != (uint32_t)geometry->first_sector ||
               max_sector != (uint32_t)(geometry->first_sector +
                                         geometry->sectors_per_track - 1))
        return false;
    return true;
}

static bool hfe_fm_walk(const uint8_t *file, size_t size,
                        hfe_geometry *geometry, bool geometry_only,
                        uint8_t *image) {
    const size_t capacity = 0x40000U;
    uint8_t *side0 = (uint8_t *)xx_mem_alloc(capacity);
    uint8_t *side1 = (uint8_t *)xx_mem_alloc(capacity);
    uint32_t limit = geometry_only ? 1U : (uint32_t)geometry->tracks;
    uint32_t track;
    bool ok = side0 != NULL && side1 != NULL;
    for (track = 0U; ok && track < limit; ++track) {
        size_t cells0 = 0U, cells1 = 0U;
        size_t entry = (size_t)geometry->lut_offset + (size_t)track * 4U;
        uint32_t side;
        /* A v1 entry gives the same byte count to each side.  Odd entries
         * cannot describe whole bytes on both sides. */
        if (entry > size || size - entry < 4U ||
            (hfe_le16(file + entry + 2U) & 1U) != 0U ||
            !hfe_track_bits(file, size, geometry, (int32_t)track,
                            side0, side1, capacity, &cells0, &cells1)) {
            ok = false;
            break;
        }
        for (side = 0U; side < (uint32_t)geometry->sides; ++side) {
            if (geometry_only && side != 0U) continue;
            if (!hfe_fm_side(side ? side1 : side0,
                             side ? cells1 : cells0, geometry, track, side,
                             geometry_only, image)) {
                ok = false;
                break;
            }
        }
    }
    if (side0) xx_mem_free(side0);
    if (side1) xx_mem_free(side1);
    return ok;
}

static bool hfe_capture_one_side(const uint8_t *bits, size_t cells,
                                 uint32_t *syncs, hfe_capture *capture,
                                 hfe_captured_side *side,
                                 uint32_t track, uint32_t physical_side,
                                 bool prefer_fm) {
    size_t checkpoint = capture->used;
    unsigned attempt;
    for (attempt = 0U; attempt < 2U; ++attempt) {
        bool fm = attempt == 0U ? prefer_fm : !prefer_fm;
        bool ok;
        xx_mem_zero(side, sizeof(*side));
        side->first = HFE_MAX_SECTORS;
        capture->used = checkpoint;
        if (fm) {
            ok = hfe_capture_fm_side(bits, cells, capture, side, track);
        } else {
            hfe_sink sink;
            xx_mem_zero(&sink, sizeof(sink));
            sink.capture = capture;
            sink.capture_side = side;
            ok = hfe_decode_side(bits, cells, syncs, &sink,
                                 (int32_t)track, (int32_t)physical_side);
        }
        if (ok && side->count != 0U) {
            side->fm = fm;
            return true;
        }
        if (side->saw_id) return false;
    }
    return false;
}

static bool hfe_capture_all(const uint8_t *file, size_t size,
                            const hfe_geometry *geometry,
                            hfe_capture *capture) {
    const size_t capacity = 0x40000U;
    uint8_t *side0 = NULL, *side1 = NULL;
    uint32_t *syncs = NULL;
    size_t side_count;
    uint32_t track, physical_side;
    bool ok = false;
    xx_mem_zero(capture, sizeof(*capture));
    side_count = (size_t)geometry->tracks * (size_t)geometry->sides;
    capture->sides = (hfe_captured_side *)xx_mem_calloc(
        side_count, sizeof(*capture->sides));
    side0 = (uint8_t *)xx_mem_alloc(capacity);
    side1 = (uint8_t *)xx_mem_alloc(capacity);
    syncs = (uint32_t *)xx_mem_alloc(HFE_MAX_SYNCS * sizeof(*syncs));
    if (!capture->sides || !side0 || !side1 || !syncs) goto done;
    for (track = 0U; track < (uint32_t)geometry->tracks; ++track) {
        size_t cells0 = 0U, cells1 = 0U;
        if (!hfe_track_bits(file, size, geometry, (int32_t)track,
                            side0, side1, capacity, &cells0, &cells1))
            goto done;
        for (physical_side = 0U;
             physical_side < (uint32_t)geometry->sides; ++physical_side) {
            hfe_captured_side *side =
                &capture->sides[(size_t)track * geometry->sides +
                                physical_side];
            if (!hfe_capture_one_side(physical_side ? side1 : side0,
                                      physical_side ? cells1 : cells0,
                                      syncs, capture, side, track,
                                      physical_side, geometry->fm))
                goto done;
        }
    }
    for (track = 0U; track < side_count; ++track) {
        const hfe_captured_side *side = &capture->sides[track];
        const hfe_captured_side *first = &capture->sides[0];
        uint32_t id;
        uint16_t first_size;
        if (side->count == 0U || side->first >= HFE_MAX_SECTORS ||
            side->last - side->first + 1U != side->count)
            goto done;
        first_size = side->sectors[side->first].size;
        if (side->count != first->count || side->first != first->first ||
            side->fm != first->fm)
            capture->variable = true;
        if (side->fm != first->fm)
            capture->size_or_encoding_variable = true;
        for (id = side->first; id <= side->last; ++id) {
            const hfe_captured_sector *sector = &side->sectors[id];
            if (!sector->present || sector->size == 0U ||
                sector->offset > capture->used ||
                sector->size > capture->used - sector->offset ||
                capture->image_size > HFE_MAX_OUTPUT - sector->size)
                goto done;
            capture->image_size += sector->size;
            if (sector->size != first_size ||
                !first->sectors[id].present ||
                sector->size != first->sectors[id].size)
                capture->variable = true;
            if (sector->size != first_size ||
                (first->sectors[id].present &&
                 sector->size != first->sectors[id].size))
                capture->size_or_encoding_variable = true;
        }
    }
    ok = capture->image_size == capture->used &&
         capture->image_size != 0U;
done:
    if (side0) xx_mem_free(side0);
    if (side1) xx_mem_free(side1);
    if (syncs) xx_mem_free(syncs);
    if (!ok) hfe_capture_free(capture);
    return ok;
}

static bool hfe_capture_render(const hfe_capture *capture,
                               const hfe_geometry *geometry,
                               uint8_t *output) {
    size_t index, cursor = 0U;
    size_t count = (size_t)geometry->tracks * geometry->sides;
    for (index = 0U; index < count; ++index) {
        const hfe_captured_side *side = &capture->sides[index];
        uint32_t id;
        for (id = side->first; id <= side->last; ++id) {
            const hfe_captured_sector *sector = &side->sectors[id];
            if (!sector->present ||
                cursor > capture->image_size ||
                sector->size > capture->image_size - cursor)
                return false;
            xx_mem_copy(output + cursor,
                        capture->bytes + sector->offset, sector->size);
            cursor += sector->size;
        }
    }
    return cursor == capture->image_size;
}

static bool hfe_probe(const uint8_t *file, size_t size, hfe_geometry *out,
                       xx_pd_struct *pd) {
    hfe_sink sink;
    hfe_capture capture;
    if ((pd && xx_pd_is_stopped(pd)) || !hfe_header(file, size, out)) return false;
    if (out->amiga) return hfe_amiga_walk(file, size, out, NULL, 0U, pd);
    if (hfe_capture_all(file, size, out, &capture)) {
        /* In FM, a lone missing address mark can otherwise masquerade as a
         * shorter final sector range.  The legacy FM path already handles
         * uniform layouts strictly; accept variable FM only when a decoded
         * sector size or encoding also changes. */
        if (capture.variable &&
            (!out->fm || capture.size_or_encoding_variable)) {
            out->image_size = capture.image_size;
            out->variable = true;
            hfe_capture_free(&capture);
            return true;
        }
        hfe_capture_free(&capture);
    }
    if (out->fm) {
        if (!hfe_fm_walk(file, size, out, true, NULL)) return false;
        out->image_size = (uint64_t)out->tracks * (uint64_t)out->sides *
                          (uint64_t)out->sectors_per_track *
                          (uint64_t)out->sector_size;
        return out->image_size != 0U &&
               out->image_size <= HFE_MAX_OUTPUT &&
               hfe_fm_walk(file, size, out, false, NULL);
    }
    xx_mem_zero(&sink, sizeof(sink));
    sink.tracks = out->tracks;
    sink.sides = out->sides;
    sink.min_sector = HFE_MAX_SECTORS;
    if (!hfe_walk(file, size, out, true, &sink) || sink.mixed ||
        sink.min_sector > sink.max_sector || sink.sector_size <= 0)
        return false;
    out->first_sector = sink.min_sector;
    out->sectors_per_track = sink.max_sector - sink.min_sector + 1;
    out->sector_size = sink.sector_size;
    out->image_size = (uint64_t)out->tracks * (uint64_t)out->sides *
                      (uint64_t)out->sectors_per_track *
                      (uint64_t)out->sector_size;
    return out->image_size != 0U && out->image_size <= HFE_MAX_OUTPUT;
}

static bool hfe_load(Abstractformat *format, int64_t base, int64_t size,
                     uint8_t **file) {
    uint8_t *data;
    if (size < HFE_HEADER_SIZE || (uint64_t)size > HFE_MAX_OUTPUT) return false;
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!data) return false;
    if (!hfe_read_at(format->device, base, data, (size_t)size)) {
        xx_mem_free(data);
        return false;
    }
    *file = data;
    return true;
}

static bool hfe_parse(Abstractformat *format, hfe_stream **result,
                       xx_pd_struct *pd) {
    uint8_t *file = NULL;
    hfe_stream *stream;
    hfe_member member;
    hfe_geometry geometry;
    int64_t total, size;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (!hfe_load(format, format->base_address, size, &file)) return false;
    if (!hfe_probe(file, (size_t)size, &geometry, pd)) {
        xx_mem_free(file);
        return false;
    }
    xx_mem_free(file);
    stream = (hfe_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = hfe_make_name("image", -1, -1,
                               geometry.amiga ? (geometry.incomplete_tracks ?
                                   ".partial.adf" : ".adf") : ".img");
    member.header_offset = format->base_address;
    member.header_size = HFE_HEADER_SIZE;
    /* The LUT scatters the flux over the whole container, so the member's
     * source extent is the container itself. */
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = geometry.image_size;
    member.method = 1U;
    member.decode = true;
    if (!member.name || !hfe_add_member(stream, &member)) {
        if (member.name) xx_mem_free(member.name);
        hfe_stream_free(stream);
        return false;
    }
    stream->archive_size = size;
    stream->recovered_cylinders = geometry.recovered_cylinders;
    stream->incomplete_tracks = geometry.incomplete_tracks;
    *result = stream;
    return true;
}

static bool hfe_decode(Abstractformat *format, const hfe_member *member,
                       uint8_t **plain, size_t *plain_size, xx_pd_struct *pd) {
    uint8_t *file = NULL;
    uint8_t *output;
    hfe_geometry geometry;
    hfe_sink sink;
    if ((pd && xx_pd_is_stopped(pd)) || member->unpacked_size == 0U || member->unpacked_size > HFE_MAX_OUTPUT ||
        !hfe_load(format, member->data_offset, member->packed_size, &file))
        return false;
    if (!hfe_probe(file, (size_t)member->packed_size, &geometry, pd) ||
        geometry.image_size != member->unpacked_size) {
        xx_mem_free(file);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!output) {
        xx_mem_free(file);
        return false;
    }
    if (geometry.amiga) {
        bool ok = hfe_amiga_walk(file, (size_t)member->packed_size,
                                 &geometry, output,
                                 (size_t)member->unpacked_size, pd);
        xx_mem_free(file);
        if (!ok) {
            xx_mem_free(output);
            return false;
        }
        *plain = output;
        *plain_size = (size_t)member->unpacked_size;
        return true;
    }
    if (geometry.variable) {
        hfe_capture capture;
        bool ok = hfe_capture_all(file, (size_t)member->packed_size,
                                  &geometry, &capture);
        if (ok)
            ok = capture.variable &&
                 capture.image_size == member->unpacked_size &&
                 hfe_capture_render(&capture, &geometry, output);
        hfe_capture_free(&capture);
        xx_mem_free(file);
        if (!ok) {
            xx_mem_free(output);
            return false;
        }
        *plain = output;
        *plain_size = (size_t)member->unpacked_size;
        return true;
    }
    /* A sector that is missing or fails its CRC stays zero so the image keeps
     * its geometry. */
    xx_mem_zero(output, (size_t)member->unpacked_size);
    xx_mem_zero(&sink, sizeof(sink));
    sink.image = output;
    sink.image_size = member->unpacked_size;
    sink.tracks = geometry.tracks;
    sink.sides = geometry.sides;
    sink.sectors_per_track = geometry.sectors_per_track;
    sink.sector_size = geometry.sector_size;
    sink.first_sector = geometry.first_sector;
    if (geometry.fm ?
            !hfe_fm_walk(file, (size_t)member->packed_size, &geometry,
                         false, output) :
            !hfe_walk(file, (size_t)member->packed_size, &geometry,
                      false, &sink)) {
        xx_mem_free(file);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(file);
    *plain = output;
    *plain_size = (size_t)member->unpacked_size;
    return true;
}

static bool hfe_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *hfe_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool hfe_set_record(xx_archive_record *record,
                           const hfe_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* Stored members are copied verbatim; everything else goes to the format
 * codec above, which is the only place a size can grow. */
static bool hfe_extract(Abstractformat *format, const hfe_member *member,
                        uint8_t **plain, size_t *plain_size, xx_pd_struct *pd) {
    uint8_t *output;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->decode) return hfe_decode(format, member, plain, plain_size, pd);
    if (member->packed_size < 0 ||
        (uint64_t)member->packed_size > HFE_MAX_OUTPUT) return false;
    output = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    if (!output) return false;
    if (member->packed_size != 0 &&
        !hfe_read_at(format->device, member->data_offset, output,
                     (size_t)member->packed_size)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->packed_size;
    return true;
}

void xx_hfe_init(xx_hfe *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HFE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hfe");
    xx_format_set_extension(&archive->format, "hfe");
    archive->format.check_is_valid = xx_hfe_check_is_valid;
    archive->format.handle_base_info = xx_hfe_handle_base_info;
    archive->format.get_format_size = xx_hfe_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hfe_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hfe_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hfe_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hfe_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hfe_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hfe_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_hfe *xx_hfe_create(xx_io_device *device, int64_t base_address) {
    xx_hfe *archive = (xx_hfe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hfe_init(archive, device, base_address);
    return archive;
}

void xx_hfe_destroy(xx_hfe *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_hfe_free(xx_hfe *archive) {
    if (!archive) return;
    xx_hfe_destroy(archive);
    xx_mem_free(archive);
}

bool xx_hfe_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    hfe_stream *stream;
    if (!hfe_parse(format, &stream, pd)) return false;
    hfe_stream_free(stream);
    return true;
}

bool xx_hfe_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    hfe_stream *stream;
    xx_hfe *archive;
    if (!format || !hfe_parse(format, &stream, pd)) return false;
    archive = (xx_hfe *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->recovered_cylinders = stream->recovered_cylinders;
    archive->incomplete_tracks = stream->incomplete_tracks;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    hfe_stream_free(stream);
    return true;
}

int64_t xx_hfe_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hfe_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_hfe_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_hfe_handle_base_info(format, pd))
               ? ((xx_hfe *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_hfe_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    hfe_stream *stream;
    xx_archive_record_state *state;
    if (!hfe_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        hfe_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = hfe_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!hfe_copy_options(&state->options, options) ||
        !hfe_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_hfe_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_hfe_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    hfe_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (hfe_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = hfe_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hfe_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    hfe_stream *stream;
    hfe_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (hfe_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!hfe_extract(format, member, &plain, &plain_size, pd) ||
        (pd && xx_pd_is_stopped(pd))) goto done;
    path_option = hfe_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        created = destination != NULL;
        if (!destination) goto done;
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

void xx_hfe_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
