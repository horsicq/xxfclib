/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Apple Disk Copy 6 NDIF image inside a MacBinary I/II/III wrapper.  The
 * field tables are in xx_apple_disk_copy_6_ndif_image.h.  Written from the
 * structure description (MacBinary header, Mac resource fork, 'bcem' 128
 * chunk table, ADC token format); libmirage's MacBinary filter (GPL) was
 * read for the layout only and no code was taken from it.
 *
 * Layout of the file:
 *   128-byte MacBinary header
 *   [secondary header, padded to 128]   (II/III with a verified CRC only)
 *   data fork, padded to 128            chunk payloads; entry offsets are
 *                                       relative to the start of this fork
 *   resource fork                       16-byte header, data, map
 *
 * Validation, in order: MacBinary header (name 1..63 printable, zero bytes
 * at 0 / 74 / 82, both forks inside the file), resource fork header and
 * map, a 'bcem' type holding id 128, and a chunk table whose first sector
 * is 0, whose sectors never decrease, whose last entry and only that entry
 * is the 0xFF terminator at the declared sector count, whose types are all
 * known, and (unsegmented images) whose payloads lie inside the data fork.
 * Every read is bounded: the probe reads the 128-byte header, 16 bytes of
 * resource-fork header, the map (at most 1 MiB) and the 'bcem' resource (at
 * most 4 MiB).
 *
 * Extraction streams: zero and raw chunks are copied in pieces and ADC
 * chunks decode through a 64 KiB ring (the longest ADC back-reference), so
 * memory use is constant whatever the image size.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/apple_disk_copy_6_ndif_image/xx_apple_disk_copy_6_ndif_image.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef APPLE_DISK_COPY_6_NDIF_IMAGE
#define XX_APPLE_DISK_COPY_6_NDIF_IMAGE_FILE_TYPE XX_FILE_TYPE_APPLE_DISK_COPY_6_NDIF_IMAGE
#else
#define XX_APPLE_DISK_COPY_6_NDIF_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NDIF_MB_HEADER 128U
#define NDIF_MB_BLOCK 128U
#define NDIF_MB_MAX_FORK UINT32_C(0x7FFFFFFF)
/* A resource fork's own offsets are 24 bits wide for resource data, and the
 * Resource Manager never wrote forks past 16 MiB. */
#define NDIF_RSRC_MAX UINT32_C(0x01000000)
#define NDIF_MAP_MAX UINT32_C(0x00100000)
#define NDIF_BCEM_MAX UINT32_C(0x00400000)
#define NDIF_BCEM_HEADER 128U
#define NDIF_ENTRY 12U
#define NDIF_SECTOR 512U

#define NDIF_T_ZERO 0x00U
#define NDIF_T_RAW 0x02U
#define NDIF_T_KENCODE 0x80U
#define NDIF_T_ADC 0x83U
#define NDIF_T_END 0xFFU

#define NDIF_ADC_WINDOW 0x10000U
#define NDIF_NAME_MAX 72U

typedef struct ndif_context_s {
    int64_t base;
    int64_t data_offset;   /**< Absolute offset of the data fork. */
    uint32_t data_size;
    int64_t rsrc_offset;
    uint32_t rsrc_size;
    int64_t format_size;
    uint32_t num_sectors;
    uint32_t num_entries;
    bool segmented;
    bool has_kencode;
    uint8_t *table;        /**< num_entries * 12 bytes, or NULL. */
    char name[NDIF_NAME_MAX];
} ndif_context;

typedef struct ndif_stream_s {
    ndif_context context;
    size_t index;
    size_t count;
} ndif_stream;

static uint32_t ndif_be16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8U) | (uint32_t)p[1];
}

static uint64_t ndif_pad128(uint64_t value) {
    return (value + (NDIF_MB_BLOCK - 1U)) & ~(uint64_t)(NDIF_MB_BLOCK - 1U);
}

static bool ndif_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > 0x100000U) request = 0x100000U;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static void ndif_context_clear(ndif_context *context) {
    if (!context) return;
    if (context->table) xx_mem_free(context->table);
    context->table = NULL;
}

/* ---------------------------------------------------------------------- */
/* Member name                                                             */

static char ndif_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool ndif_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || ndif_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

static bool ndif_is_device_stem(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length = xx_str_len(name), stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (ndif_stem_is(name, stem, devices[index])) return true;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((ndif_upper(name[0]) == 'C' && ndif_upper(name[1]) == 'O' &&
          ndif_upper(name[2]) == 'M') ||
         (ndif_upper(name[0]) == 'L' && ndif_upper(name[1]) == 'P' &&
          ndif_upper(name[2]) == 'T')))
        return true;
    return false;
}

/* The one member is named after the image name in 'bcem' (else the
 * MacBinary file name), reduced to a safe single path component: printable
 * ASCII only, separators / drive colons / Windows-reserved punctuation and
 * Mac Roman bytes become '_', leading and trailing dots and spaces are
 * dropped (so "." and ".." cannot survive), a device stem gets a '_' prefix,
 * and ".img" is appended unless already present. */
static void ndif_make_name(const uint8_t *raw, size_t length, char *out) {
    char tmp[NDIF_NAME_MAX];
    size_t index, used = 0U, start = 0U, end;
    if (length > 63U) length = 63U;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        char mapped = (char)c;
        if (c < 0x20U || c > 0x7EU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            mapped = '_';
        tmp[used++] = mapped;
    }
    tmp[used] = 0;
    end = used;
    while (start < end && (tmp[start] == '.' || tmp[start] == ' ')) ++start;
    while (end > start && (tmp[end - 1U] == '.' || tmp[end - 1U] == ' '))
        --end;
    used = 0U;
    if (end == start) {
        xx_rt_memcpy(out, "disk", 4U);
        used = 4U;
    } else {
        tmp[end] = 0;
        if (ndif_is_device_stem(tmp + start)) out[used++] = '_';
        xx_rt_memcpy(out + used, tmp + start, end - start);
        used += end - start;
    }
    out[used] = 0;
    if (!(used >= 4U && out[used - 4U] == '.' &&
          ndif_upper(out[used - 3U]) == 'I' &&
          ndif_upper(out[used - 2U]) == 'M' &&
          ndif_upper(out[used - 1U]) == 'G')) {
        xx_rt_memcpy(out + used, ".img", 5U);
    }
}

/* ---------------------------------------------------------------------- */
/* Parser                                                                  */

/* MacBinary header.  Fills the fork extents; everything else is left to the
 * resource fork, which is what actually identifies NDIF. */
static bool ndif_parse_macbinary(const uint8_t *h, int64_t base, int64_t total,
                                 ndif_context *c) {
    uint32_t name_length = h[1], index, secondary = 0U;
    uint64_t data_offset, rsrc_offset, end;
    bool verified;
    if (h[0] != 0U || name_length < 1U || name_length > 63U || h[74] != 0U ||
        h[82] != 0U)
        return false;
    for (index = 0U; index < name_length; ++index)
        if (h[2U + index] < 0x20U || h[2U + index] == 0x7FU) return false;
    verified = xx_crc16_xmodem_calc(0U, h, 124U) == ndif_be16(h + 124U) &&
               (h[122] >= 129U || ndif_be16(h + 124U) != 0U);
    if (verified) secondary = ndif_be16(h + 120U);
    c->data_size = xx_data_get_u32(h + 83U, 4, 0, true);
    c->rsrc_size = xx_data_get_u32(h + 87U, 4, 0, true);
    if (c->data_size > NDIF_MB_MAX_FORK || c->rsrc_size > NDIF_RSRC_MAX ||
        c->rsrc_size < 16U + 30U)
        return false;
    data_offset = (uint64_t)base + NDIF_MB_HEADER + ndif_pad128(secondary);
    rsrc_offset = data_offset + ndif_pad128(c->data_size);
    end = rsrc_offset + c->rsrc_size;
    if (end > (uint64_t)total) return false;
    c->data_offset = (int64_t)data_offset;
    c->rsrc_offset = (int64_t)rsrc_offset;
    /* The resource fork is normally padded to a block; accept a file whose
     * last padding was dropped.  A verified header's comment follows. */
    end = rsrc_offset + ndif_pad128(c->rsrc_size);
    if (verified && ndif_be16(h + 99U) != 0U)
        end += ndif_pad128(ndif_be16(h + 99U));
    if (end > (uint64_t)total) end = (uint64_t)total;
    c->format_size = (int64_t)(end - (uint64_t)base);
    return true;
}

/* Finds 'bcem' 128 and returns its absolute offset and size. */
static bool ndif_find_bcem(xx_io_device *device, const ndif_context *c,
                           int64_t *offset_out, uint32_t *size_out) {
    uint8_t head[16], length_bytes[4];
    uint8_t *map = NULL;
    uint32_t data_off, map_off, data_len, map_len, types_off, type_count,
             index;
    bool result = false;
    if (!ndif_read_at(device, c->rsrc_offset, head, sizeof(head)))
        return false;
    data_off = xx_data_get_u32(head, 4, 0, true);
    map_off = xx_data_get_u32(head + 4U, 4, 0, true);
    data_len = xx_data_get_u32(head + 8U, 4, 0, true);
    map_len = xx_data_get_u32(head + 12U, 4, 0, true);
    if (data_off < 16U || data_off > c->rsrc_size ||
        data_len > c->rsrc_size - data_off || map_off < 16U ||
        map_off > c->rsrc_size || map_len < 30U ||
        map_len > c->rsrc_size - map_off || map_len > NDIF_MAP_MAX)
        return false;
    map = (uint8_t *)xx_mem_alloc(map_len);
    if (!map) return false;
    if (!ndif_read_at(device, c->rsrc_offset + map_off, map, map_len))
        goto done;
    types_off = ndif_be16(map + 24U);
    if (types_off < 28U || types_off > map_len - 2U) goto done;
    type_count = (ndif_be16(map + types_off) + 1U) & 0xFFFFU;
    if ((uint64_t)types_off + 2U + (uint64_t)type_count * 8U > map_len)
        goto done;
    for (index = 0U; index < type_count; ++index) {
        const uint8_t *t = map + types_off + 2U + index * 8U;
        uint32_t ref_count, ref_off, ref;
        if (xx_rt_memcmp(t, "bcem", 4U) != 0) continue;
        ref_count = ndif_be16(t + 4U) + 1U;
        ref_off = types_off + ndif_be16(t + 6U);
        if ((uint64_t)ref_off + (uint64_t)ref_count * 12U > map_len)
            goto done;
        for (ref = 0U; ref < ref_count; ++ref) {
            const uint8_t *r = map + ref_off + ref * 12U;
            uint32_t item, size;
            if (ndif_be16(r) != 128U) continue;
            item = xx_data_get_u24(r + 5U, 3, 0, true);
            if (item > data_len || data_len - item < 4U) goto done;
            if (!ndif_read_at(device,
                              c->rsrc_offset + data_off + item, length_bytes,
                              4U))
                goto done;
            size = xx_data_get_u32(length_bytes, 4, 0, true);
            if (size > data_len - item - 4U) goto done;
            *offset_out = c->rsrc_offset + data_off + item + 4U;
            *size_out = size;
            result = true;
            goto done;
        }
        goto done;
    }
done:
    xx_mem_free(map);
    return result;
}

/* Validates the chunk table in @p b (the whole 'bcem' resource). */
static bool ndif_check_table(const uint8_t *b, uint32_t size, ndif_context *c) {
    uint32_t count, index, previous = 0U;
    if (size < NDIF_BCEM_HEADER || b[4] > 63U) return false;
    c->num_sectors = xx_data_get_u32(b + 0x44U, 4, 0, true);
    c->segmented = xx_data_get_u32(b + 0x54U, 4, 0, true) != 0U;
    count = xx_data_get_u32(b + 0x7CU, 4, 0, true);
    if (count < 1U || count > (size - NDIF_BCEM_HEADER) / NDIF_ENTRY)
        return false;
    /* The terminator's 24-bit sector must equal the count. */
    if (c->num_sectors > 0xFFFFFFU) return false;
    c->num_entries = count;
    c->has_kencode = false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *e = b + NDIF_BCEM_HEADER + index * NDIF_ENTRY;
        uint32_t sector = xx_data_get_u24(e, 3, 0, true), type = e[3];
        uint32_t offset = xx_data_get_u32(e + 4U, 4, 0, true), length = xx_data_get_u32(e + 8U, 4, 0, true);
        if (index == 0U ? sector != 0U : sector < previous) return false;
        previous = sector;
        if (index + 1U == count) {
            if (type != NDIF_T_END || sector != c->num_sectors) return false;
            break;
        }
        switch (type) {
        case NDIF_T_ZERO:
            break;
        case NDIF_T_RAW:
        case NDIF_T_ADC:
        case NDIF_T_KENCODE:
            if (type == NDIF_T_KENCODE) c->has_kencode = true;
            if (!c->segmented &&
                (offset > c->data_size || length > c->data_size - offset))
                return false;
            break;
        default:
            return false;
        }
    }
    return true;
}

static bool ndif_parse(Abstractformat *format, ndif_context *out,
                       bool keep_table) {
    uint8_t header[NDIF_MB_HEADER];
    ndif_context c;
    int64_t total, bcem_offset = 0;
    uint32_t bcem_size = 0U;
    uint8_t *bcem = NULL;
    bool ok = false;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address ||
        total - format->base_address < (int64_t)(NDIF_MB_HEADER + 46U))
        return false;
    if (!ndif_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    xx_mem_zero(&c, sizeof(c));
    c.base = format->base_address;
    if (!ndif_parse_macbinary(header, format->base_address, total, &c))
        return false;
    if (!ndif_find_bcem(format->device, &c, &bcem_offset, &bcem_size) ||
        bcem_size < NDIF_BCEM_HEADER || bcem_size > NDIF_BCEM_MAX)
        return false;
    bcem = (uint8_t *)xx_mem_alloc(bcem_size);
    if (!bcem) return false;
    if (!ndif_read_at(format->device, bcem_offset, bcem, bcem_size) ||
        !ndif_check_table(bcem, bcem_size, &c))
        goto done;
    if (bcem[4] != 0U)
        ndif_make_name(bcem + 5U, bcem[4], c.name);
    else
        ndif_make_name(header + 2U, header[1], c.name);
    if (keep_table) {
        size_t table_size = (size_t)c.num_entries * NDIF_ENTRY;
        c.table = (uint8_t *)xx_mem_alloc(table_size);
        if (!c.table) goto done;
        xx_rt_memcpy(c.table, bcem + NDIF_BCEM_HEADER, table_size);
    }
    *out = c;
    ok = true;
done:
    xx_mem_free(bcem);
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Output                                                                  */

typedef struct ndif_sink_s {
    xx_io_device *device;
    uint8_t *memory;       /**< Memory target of capacity bytes, or NULL. */
    uint64_t capacity;
    uint64_t written;
} ndif_sink;

static bool ndif_emit(ndif_sink *sink, const uint8_t *data, size_t size) {
    size_t done = 0U;
    if (sink->memory) {
        if ((uint64_t)size > sink->capacity - sink->written) return false;
        if (size) xx_rt_memcpy(sink->memory + sink->written, data, size);
    } else if (sink->device) {
        while (done < size) {
            ssize_t wrote = xx_io_write(sink->device, data + done, size - done);
            if (wrote <= 0 || (size_t)wrote > size - done) return false;
            done += (size_t)wrote;
        }
    }
    sink->written += size;
    return true;
}

/* ---------------------------------------------------------------------- */
/* ADC                                                                     */

typedef struct ndif_input_s {
    xx_io_device *device;
    const uint8_t *memory;
    int64_t offset;
    uint64_t remaining;
    uint64_t consumed;
    uint8_t *buffer;
    size_t capacity;
    size_t length;
    size_t position;
} ndif_input;

static bool ndif_in_byte(ndif_input *in, uint8_t *value) {
    if (in->position == in->length) {
        size_t amount;
        if (in->remaining == 0U) return false;
        amount = in->remaining < (uint64_t)in->capacity
                     ? (size_t)in->remaining : in->capacity;
        if (in->memory) {
            xx_rt_memcpy(in->buffer, in->memory, amount);
            in->memory += amount;
        } else if (!ndif_read_at(in->device, in->offset, in->buffer, amount)) {
            return false;
        }
        in->offset += (int64_t)amount;
        in->remaining -= amount;
        in->length = amount;
        in->position = 0U;
    }
    *value = in->buffer[in->position++];
    ++in->consumed;
    return true;
}

/* ADC tokens (first byte b):
 *   b & 0x80          literal run of (b & 0x7F) + 1 bytes
 *   b & 0x40          match of (b & 0x3F) + 4 bytes, distance = u16 + 1
 *   otherwise         match of ((b >> 2) & 0x0F) + 3 bytes,
 *                     distance = (((b & 3) << 8) | next) + 1
 * Distances reach 65536, so a 64 KiB ring holds all the history needed;
 * each byte of a match is read from the ring before its own slot is
 * overwritten, which makes distance 65536 (the slot itself) correct too. */
static bool ndif_adc(ndif_input *in, uint64_t expected, uint8_t *ring,
                     ndif_sink *sink, xx_pd_struct *pd) {
    uint64_t produced = 0U;
    uint32_t position = 0U, flushed = 0U;
    unsigned long tokens = 0UL;
    while (produced < expected) {
        uint8_t b, x, y;
        uint32_t length, distance, index;
        if ((++tokens & 0xFFFFUL) == 0UL && pd && xx_pd_is_stopped(pd))
            return false;
        if (!ndif_in_byte(in, &b)) return false;
        if (b & 0x80U) {
            length = (uint32_t)(b & 0x7FU) + 1U;
            if ((uint64_t)length > expected - produced) return false;
            for (index = 0U; index < length; ++index) {
                if (!ndif_in_byte(in, &x)) return false;
                ring[position++] = x;
                if (position == NDIF_ADC_WINDOW) {
                    if (!ndif_emit(sink, ring + flushed, position - flushed))
                        return false;
                    position = flushed = 0U;
                }
            }
        } else {
            if (b & 0x40U) {
                if (!ndif_in_byte(in, &x) || !ndif_in_byte(in, &y))
                    return false;
                length = (uint32_t)(b & 0x3FU) + 4U;
                distance = (((uint32_t)x << 8U) | y) + 1U;
            } else {
                if (!ndif_in_byte(in, &x)) return false;
                length = (uint32_t)((b >> 2U) & 0x0FU) + 3U;
                distance = ((((uint32_t)b & 3U) << 8U) | x) + 1U;
            }
            if ((uint64_t)distance > produced ||
                (uint64_t)length > expected - produced)
                return false;
            for (index = 0U; index < length; ++index) {
                ring[position] =
                    ring[(position - distance) & (NDIF_ADC_WINDOW - 1U)];
                ++position;
                if (position == NDIF_ADC_WINDOW) {
                    if (!ndif_emit(sink, ring + flushed, position - flushed))
                        return false;
                    position = flushed = 0U;
                }
            }
        }
        produced += length;
    }
    return ndif_emit(sink, ring + flushed, position - flushed);
}

bool xx_apple_disk_copy_6_ndif_image_adc_decode_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed) {
    ndif_input in;
    ndif_sink sink;
    uint8_t *ring;
    uint8_t *staging;
    bool result;
    if (consumed) *consumed = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    ring = (uint8_t *)xx_mem_alloc(NDIF_ADC_WINDOW);
    staging = (uint8_t *)xx_mem_alloc(0x1000U);
    if (!ring || !staging) {
        if (ring) xx_mem_free(ring);
        if (staging) xx_mem_free(staging);
        return false;
    }
    xx_mem_zero(&in, sizeof(in));
    in.memory = input;
    in.remaining = input_size;
    in.buffer = staging;
    in.capacity = 0x1000U;
    xx_mem_zero(&sink, sizeof(sink));
    sink.memory = output;
    sink.capacity = output_size;
    result = ndif_adc(&in, output_size, ring, &sink, NULL) &&
             sink.written == output_size;
    if (result && consumed) *consumed = (size_t)in.consumed;
    xx_mem_free(ring);
    xx_mem_free(staging);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Image                                                                   */

static bool ndif_unpack_context(Abstractformat *format,
                                const ndif_context *c,
                                xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *ring = NULL, *buffer = NULL;
    const size_t buffer_size = 0x10000U;
    ndif_sink sink;
    uint32_t index;
    bool result = false;
    if (!format || !c || !c->table) return false;
    ring = (uint8_t *)xx_mem_alloc(NDIF_ADC_WINDOW);
    buffer = (uint8_t *)xx_mem_alloc(buffer_size);
    if (!ring || !buffer) goto done;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device = destination;
    for (index = 0U; index + 1U < c->num_entries; ++index) {
        const uint8_t *e = c->table + (size_t)index * NDIF_ENTRY;
        uint32_t type = e[3];
        uint32_t offset = xx_data_get_u32(e + 4U, 4, 0, true), length = xx_data_get_u32(e + 8U, 4, 0, true);
        uint64_t sectors = (uint64_t)xx_data_get_u24(e + NDIF_ENTRY, 3, 0, true) - xx_data_get_u24(e, 3, 0, true);
        uint64_t bytes = sectors * NDIF_SECTOR;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (sectors == 0U) continue;
        if (type == NDIF_T_ZERO) {
            xx_mem_zero(buffer, buffer_size);
            while (bytes) {
                size_t step = bytes < buffer_size ? (size_t)bytes : buffer_size;
                if (!ndif_emit(&sink, buffer, step)) goto done;
                bytes -= step;
                if (pd && xx_pd_is_stopped(pd)) goto done;
            }
            continue;
        }
        if (offset > c->data_size || length > c->data_size - offset) goto done;
        if (type == NDIF_T_RAW) {
            int64_t at = c->data_offset + offset;
            if ((uint64_t)length < bytes) goto done;
            while (bytes) {
                size_t step = bytes < buffer_size ? (size_t)bytes : buffer_size;
                if (!ndif_read_at(format->device, at, buffer, step) ||
                    !ndif_emit(&sink, buffer, step))
                    goto done;
                at += (int64_t)step;
                bytes -= step;
                if (pd && xx_pd_is_stopped(pd)) goto done;
            }
        } else if (type == NDIF_T_ADC) {
            ndif_input in;
            xx_mem_zero(&in, sizeof(in));
            in.device = format->device;
            in.offset = c->data_offset + offset;
            in.remaining = length;
            in.buffer = buffer;
            in.capacity = buffer_size;
            if (!ndif_adc(&in, bytes, ring, &sink, pd)) goto done;
        } else {
            /* KenCode (0x80): no decoder is known. */
            goto done;
        }
    }
    result = sink.written == (uint64_t)c->num_sectors * NDIF_SECTOR;
done:
    if (ring) xx_mem_free(ring);
    if (buffer) xx_mem_free(buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void ndif_stream_free(void *opaque) {
    ndif_stream *stream = (ndif_stream *)opaque;
    if (!stream) return;
    ndif_context_clear(&stream->context);
    xx_mem_free(stream);
}

static bool ndif_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
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

static const xx_var *ndif_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ndif_set_record(xx_archive_record *record,
                            const ndif_context *c) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = c->base;
    record->header_size = NDIF_MB_HEADER;
    record->data_offset = c->data_offset;
    record->compressed_size = (int64_t)c->data_size;
    return xx_archive_record_set_original_name(record, c->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)c->data_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)c->num_sectors * NDIF_SECTOR) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_apple_disk_copy_6_ndif_image_init(
    xx_apple_disk_copy_6_ndif_image *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_APPLE_DISK_COPY_6_NDIF_IMAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-macbinary");
    xx_format_set_extension(&archive->format, "img");
    archive->format.check_is_valid =
        xx_apple_disk_copy_6_ndif_image_check_is_valid;
    archive->format.handle_base_info =
        xx_apple_disk_copy_6_ndif_image_handle_base_info;
    archive->format.get_format_size =
        xx_apple_disk_copy_6_ndif_image_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_apple_disk_copy_6_ndif_image_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_apple_disk_copy_6_ndif_image_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_apple_disk_copy_6_ndif_image_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_apple_disk_copy_6_ndif_image_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_apple_disk_copy_6_ndif_image_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_apple_disk_copy_6_ndif_image_free_archive_records_reading;
}

xx_apple_disk_copy_6_ndif_image *xx_apple_disk_copy_6_ndif_image_create(
    xx_io_device *device, int64_t base_address) {
    xx_apple_disk_copy_6_ndif_image *archive =
        (xx_apple_disk_copy_6_ndif_image *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_apple_disk_copy_6_ndif_image_init(archive, device, base_address);
    return archive;
}

void xx_apple_disk_copy_6_ndif_image_destroy(
    xx_apple_disk_copy_6_ndif_image *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_apple_disk_copy_6_ndif_image_free(
    xx_apple_disk_copy_6_ndif_image *archive) {
    if (!archive) return;
    xx_apple_disk_copy_6_ndif_image_destroy(archive);
    xx_mem_free(archive);
}

bool xx_apple_disk_copy_6_ndif_image_check_is_valid(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    ndif_context c;
    (void)pd;
    return ndif_parse(format, &c, false);
}

bool xx_apple_disk_copy_6_ndif_image_handle_base_info(Abstractformat *format,
                                                      xx_pd_struct *pd) {
    ndif_context c;
    xx_apple_disk_copy_6_ndif_image *archive;
    (void)pd;
    if (!format || !ndif_parse(format, &c, false)) return false;
    archive = (xx_apple_disk_copy_6_ndif_image *)format;
    archive->number_of_records = 1U;
    archive->image_size = (uint64_t)c.num_sectors * NDIF_SECTOR;
    archive->chunk_count = c.num_entries;
    archive->segmented = c.segmented;
    archive->has_kencode = c.has_kencode;
    format->number_of_archive_records = 1U;
    format->format_size = c.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_apple_disk_copy_6_ndif_image_get_format_size(Abstractformat *format,
                                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_apple_disk_copy_6_ndif_image_handle_base_info(format,
                                                                       pd))
               ? format->format_size : -1;
}

uint64_t xx_apple_disk_copy_6_ndif_image_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_apple_disk_copy_6_ndif_image_handle_base_info(format,
                                                                       pd))
               ? ((xx_apple_disk_copy_6_ndif_image *)format)->number_of_records
               : 0U;
}

bool xx_apple_disk_copy_6_ndif_image_unpack_to_device(
    xx_apple_disk_copy_6_ndif_image *archive, xx_io_device *destination,
    xx_pd_struct *pd) {
    ndif_context c;
    bool result;
    if (!archive || !ndif_parse(&archive->format, &c, true)) return false;
    result = ndif_unpack_context(&archive->format, &c, destination, pd);
    ndif_context_clear(&c);
    return result;
}

xx_archive_record_state *
xx_apple_disk_copy_6_ndif_image_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ndif_stream *stream;
    xx_archive_record_state *state;
    ndif_context c;
    (void)pd;
    if (!ndif_parse(format, &c, true)) return NULL;
    stream = (ndif_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        ndif_context_clear(&c);
        return NULL;
    }
    stream->context = c;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ndif_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ndif_stream_free;
    state->total_records = 1U;
    if (!ndif_copy_options(&state->options, options) ||
        !ndif_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_apple_disk_copy_6_ndif_image_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_apple_disk_copy_6_ndif_image_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    ndif_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ndif_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_apple_disk_copy_6_ndif_image_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    ndif_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ndif_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = ndif_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return ndif_unpack_context(format, &stream->context, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->context.name)
               : xx_str_concat(base, stream->context.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = ndif_unpack_context(format, &stream->context, destination,
                                     pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_apple_disk_copy_6_ndif_image_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
