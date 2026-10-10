/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Expert Witness Compression Format 2 (EnCase 7+ .Ex01). The layout is
 * documented in xx_ewf2_ex01.h; this file was written from that description
 * (libyal's published EWF2 format notes), not from any implementation.
 *
 * The reader works in two steps:
 *
 *   CHAIN   find every section of the segment. Descriptors sit at the END
 *           of their section and point backwards, so the chain is followed
 *           from the "done"/"next" descriptor that closes the device. When
 *           the device carries more than the segment (a carved file, a
 *           padded copy), each descriptor is found instead by scanning
 *           forward at 16-byte steps for the one that points back at the
 *           previous descriptor and carries a correct Adler-32.
 *   PASS    walk the sections in file order. INFO reads the metadata, the
 *           table headers and the stored hashes; DECODE additionally reads
 *           each table's entries and writes its chunks out in order.
 *
 * Every descriptor must carry a correct Adler-32 and point strictly
 * backwards, so the chain cannot loop. Chunk numbering has to be contiguous
 * across tables, so the output is never more than the declared media size,
 * and each chunk is produced into a buffer exactly one chunk long.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ewf2_ex01/xx_ewf2_ex01.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef EWF2_EX01
#define XX_EWF2_EX01_FILE_TYPE XX_FILE_TYPE_EWF2_EX01
#else
#define XX_EWF2_EX01_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_EWF2_HEADER_SIZE 32
#define XX_EWF2_DESCRIPTOR_SIZE 64
#define XX_EWF2_TABLE_HEADER_SIZE 32U
#define XX_EWF2_TABLE_ENTRY_SIZE 16U
#define XX_EWF2_TABLE_FOOTER_SIZE 4U
#define XX_EWF2_MD5_SECTION_SIZE 20U
#define XX_EWF2_SHA1_SECTION_SIZE 24U

#define XX_EWF2_TYPE_DEVICE_INFORMATION 0x01U
#define XX_EWF2_TYPE_CASE_DATA 0x02U
#define XX_EWF2_TYPE_SECTOR_DATA 0x03U
#define XX_EWF2_TYPE_SECTOR_TABLE 0x04U
#define XX_EWF2_TYPE_MD5 0x08U
#define XX_EWF2_TYPE_SHA1 0x09U
#define XX_EWF2_TYPE_ENCRYPTION_KEYS 0x0BU
#define XX_EWF2_TYPE_NEXT 0x0DU
#define XX_EWF2_TYPE_DONE 0x0FU

#define XX_EWF2_FLAG_ENCRYPTED 0x02U

#define XX_EWF2_CHUNK_COMPRESSED 0x01U
#define XX_EWF2_CHUNK_CHECKSUMMED 0x02U
#define XX_EWF2_CHUNK_PATTERN_FILL 0x04U

/* A section costs at least a descriptor, so this only bounds what a crafted
 * file can ask for; EnCase writes a few sections per 1600 chunks. */
#define XX_EWF2_MAX_SECTIONS UINT32_C(0x100000)
/* The first section (device information) is a few hundred bytes; the
 * probe's forward search for it stops after this much. */
#define XX_EWF2_PROBE_SCAN (INT64_C(1) << 20)
#define XX_EWF2_SCAN_BLOCK 65536U
/* EnCase limits a text value to 3000 characters. */
#define XX_EWF2_MAX_META_PACKED (1U << 20)
#define XX_EWF2_MAX_META_TEXT (1U << 20)
/* EnCase offers at most 1024 sectors per chunk. */
#define XX_EWF2_MAX_CHUNK_SIZE (UINT32_C(16) << 20)
#define XX_EWF2_MAX_BYTES_PER_SECTOR UINT32_C(65536)
#define XX_EWF2_MAX_TABLE_ENTRIES UINT32_C(0x400000)
#define XX_EWF2_ENTRY_BATCH 4096U

#define XX_EWF2_MEMBER_NAME "disk.img"

static const uint8_t xx_ewf2_signature[8] = {0x45U, 0x56U, 0x46U, 0x32U, 0x0DU, 0x0AU, 0x81U, 0x00U};

typedef struct xx_ewf2_section_s {
    int64_t desc_at;    /**< Absolute offset of the descriptor. */
    int64_t data_at;    /**< Absolute offset of the data. */
    uint64_t data_size; /**< Padding included. */
    uint32_t padding_size;
    uint32_t type;
    uint32_t flags;
} xx_ewf2_section;

typedef struct xx_ewf2_private_s {
    int64_t input_size;
    int64_t base_address;
    int64_t format_end;
    int64_t first_data;
    xx_ewf2_section *sections;
    uint32_t section_count;
    uint32_t section_capacity;
    uint64_t media_size;
    uint64_t number_of_sectors;
    uint64_t number_of_chunks;
    uint64_t table_entries;
    uint32_t sectors_per_chunk;
    uint32_t bytes_per_sector;
    uint32_t chunk_size;
    uint32_t segment_number;
    uint16_t method;
    uint8_t minor_version;
    bool has_geometry;
    bool encrypted;
    bool last_segment;
    bool contiguous;
    bool complete;
    bool has_md5;
    bool has_sha1;
    bool consumed;
    uint8_t md5[16];
    uint8_t sha1[20];
    uint8_t guid[16];
} xx_ewf2_private;

typedef struct xx_ewf2_decoder_s {
    xx_io_device *output;       /**< NULL: decode and verify only. */
    xx_io_device *chunk_device; /**< Fixed memory device over chunk. */
    uint8_t *chunk;             /**< chunk_size + 4 bytes. */
    uint8_t *packed;
    size_t packed_capacity;
    uint8_t *entries; /**< XX_EWF2_ENTRY_BATCH entries. */
    uint64_t next_chunk;
    xx_hash_context md5;
    xx_hash_context sha1;
} xx_ewf2_decoder;

/* Values the metadata strings give. */
typedef struct xx_ewf2_meta_s {
    uint64_t sectors;
    uint64_t bytes_per_sector;
    uint64_t sectors_per_chunk;
    uint64_t chunks;
    bool has_sectors;
    bool has_bytes_per_sector;
    bool has_sectors_per_chunk;
    bool has_chunks;
} xx_ewf2_meta;

static void xx_ewf2_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ewf2_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_ewf2_range_within(int64_t total_size, int64_t offset, int64_t size)
{
    return total_size >= 0 && offset >= 0 && size >= 0 && offset <= total_size && size <= total_size - offset;
}

static bool xx_ewf2_write_all(xx_io_device *output, const uint8_t *data, size_t size)
{
    size_t done = 0U;

    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

static bool xx_ewf2_is_zero(const uint8_t *data, size_t size)
{
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (data[index] != 0U) return false;
    }
    return true;
}

static bool xx_ewf2_known_type(uint32_t type)
{
    return (type >= 0x01U && type <= 0x10U) || (type >= 0x20U && type <= 0x23U);
}

static bool xx_ewf2_is_terminal(uint32_t type)
{
    return type == XX_EWF2_TYPE_NEXT || type == XX_EWF2_TYPE_DONE;
}

static void xx_ewf2_private_reset(xx_ewf2_private *parsed)
{
    if (parsed->sections) xx_mem_free(parsed->sections);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->first_data = -1;
    parsed->contiguous = true;
}

static void xx_ewf2_private_free(void *pointer)
{
    xx_ewf2_private *parsed = (xx_ewf2_private *)pointer;

    if (!parsed) return;
    if (parsed->sections) xx_mem_free(parsed->sections);
    xx_mem_free(parsed);
}

/* ------------------------------------------------------------ structure -- */

static bool xx_ewf2_read_header(xx_io_device *device, xx_ewf2_private *parsed)
{
    uint8_t header[XX_EWF2_HEADER_SIZE];

    if (!xx_ewf2_range_within(parsed->input_size, parsed->base_address, XX_EWF2_HEADER_SIZE + XX_EWF2_DESCRIPTOR_SIZE) ||
        !xx_ewf2_read_at(device, parsed->base_address, header, sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, xx_ewf2_signature, sizeof(xx_ewf2_signature)) != 0 || header[8] != 2U) {
        return false;
    }
    parsed->minor_version = header[9];
    parsed->method = xx_data_get_u16(header, sizeof(header), 10U, false);
    parsed->segment_number = xx_data_get_u32(header, sizeof(header), 12U, false);
    xx_rt_memcpy(parsed->guid, header + 16, 16U);
    return parsed->method <= 2U && parsed->segment_number != 0U;
}

/* Decode and check the descriptor in raw, which lies at absolute desc_at.
 * The section's data then starts right after the previous descriptor. */
static bool xx_ewf2_parse_descriptor(const uint8_t *raw, int64_t base, int64_t desc_at, xx_ewf2_section *out, uint64_t *previous)
{
    uint64_t prev;
    uint64_t relative = (uint64_t)(desc_at - base);
    int64_t start;

    if (xx_data_get_u32(raw, XX_EWF2_DESCRIPTOR_SIZE, 24U, false) != XX_EWF2_DESCRIPTOR_SIZE ||
        xx_adler32(raw, 60U) != xx_data_get_u32(raw, XX_EWF2_DESCRIPTOR_SIZE, 60U, false)) {
        return false;
    }
    out->type = xx_data_get_u32(raw, XX_EWF2_DESCRIPTOR_SIZE, 0U, false);
    out->flags = xx_data_get_u32(raw, XX_EWF2_DESCRIPTOR_SIZE, 4U, false);
    prev = xx_data_get_u64(raw, XX_EWF2_DESCRIPTOR_SIZE, 8U, false);
    out->data_size = xx_data_get_u64(raw, XX_EWF2_DESCRIPTOR_SIZE, 16U, false);
    out->padding_size = xx_data_get_u32(raw, XX_EWF2_DESCRIPTOR_SIZE, 28U, false);
    if (!xx_ewf2_known_type(out->type)) return false;
    if (prev == 0U) {
        start = base + XX_EWF2_HEADER_SIZE;
    } else {
        if (prev < XX_EWF2_HEADER_SIZE || prev > relative - XX_EWF2_DESCRIPTOR_SIZE || relative < XX_EWF2_DESCRIPTOR_SIZE) {
            return false;
        }
        start = base + (int64_t)prev + XX_EWF2_DESCRIPTOR_SIZE;
    }
    if (start > desc_at || out->data_size > (uint64_t)(desc_at - start) || (uint64_t)out->padding_size > out->data_size) {
        return false;
    }
    if (xx_ewf2_is_terminal(out->type) && out->data_size != 0U) return false;
    out->desc_at = desc_at;
    out->data_at = start;
    *previous = prev;
    return true;
}

static bool xx_ewf2_read_descriptor(xx_io_device *device, const xx_ewf2_private *parsed, int64_t desc_at, xx_ewf2_section *out, uint64_t *previous)
{
    uint8_t raw[XX_EWF2_DESCRIPTOR_SIZE];

    if (desc_at < parsed->base_address + XX_EWF2_HEADER_SIZE || !xx_ewf2_range_within(parsed->input_size, desc_at, XX_EWF2_DESCRIPTOR_SIZE) ||
        !xx_ewf2_read_at(device, desc_at, raw, sizeof(raw))) {
        return false;
    }
    return xx_ewf2_parse_descriptor(raw, parsed->base_address, desc_at, out, previous);
}

static bool xx_ewf2_push(xx_ewf2_private *parsed, const xx_ewf2_section *section)
{
    if (parsed->section_count >= XX_EWF2_MAX_SECTIONS) return false;
    if (parsed->section_count == parsed->section_capacity) {
        uint32_t capacity = parsed->section_capacity ? parsed->section_capacity * 2U : 64U;
        xx_ewf2_section *grown;
        if (capacity > XX_EWF2_MAX_SECTIONS) capacity = XX_EWF2_MAX_SECTIONS;
        grown = (xx_ewf2_section *)xx_mem_alloc((size_t)capacity * sizeof(xx_ewf2_section));
        if (!grown) return false;
        if (parsed->sections) {
            xx_rt_memcpy(grown, parsed->sections, (size_t)parsed->section_count * sizeof(xx_ewf2_section));
            xx_mem_free(parsed->sections);
        }
        parsed->sections = grown;
        parsed->section_capacity = capacity;
    }
    parsed->sections[parsed->section_count++] = *section;
    return true;
}

/* Follow the chain back from the descriptor that ends the device. */
static bool xx_ewf2_chain_backward(xx_io_device *device, xx_ewf2_private *parsed, bool probe, xx_pd_struct *pd)
{
    xx_ewf2_section section;
    uint64_t previous = 0U;
    int64_t at = parsed->input_size - XX_EWF2_DESCRIPTOR_SIZE;
    uint32_t low;
    uint32_t high;

    if (!xx_ewf2_read_descriptor(device, parsed, at, &section, &previous) || !xx_ewf2_is_terminal(section.type)) {
        return false;
    }
    if (probe) return true;
    parsed->section_count = 0U;
    for (;;) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_ewf2_push(parsed, &section)) return false;
        if (previous == 0U) break;
        at = parsed->base_address + (int64_t)previous;
        if (!xx_ewf2_read_descriptor(device, parsed, at, &section, &previous) || xx_ewf2_is_terminal(section.type)) {
            return false;
        }
    }
    /* Collected last to first. */
    for (low = 0U, high = parsed->section_count - 1U; low < high; ++low, --high) {
        xx_ewf2_section swap = parsed->sections[low];
        parsed->sections[low] = parsed->sections[high];
        parsed->sections[high] = swap;
    }
    parsed->format_end = parsed->input_size;
    return true;
}

/* Find the first descriptor at or after from (16-byte aligned from the
 * segment start) that points back at previous. */
static bool xx_ewf2_scan_descriptor(xx_io_device *device, const xx_ewf2_private *parsed, int64_t from, uint64_t previous, int64_t limit, uint8_t *buffer,
                                    xx_ewf2_section *out, xx_pd_struct *pd)
{
    int64_t block = from;

    if (limit > parsed->input_size) limit = parsed->input_size;
    while (block <= limit - XX_EWF2_DESCRIPTOR_SIZE) {
        int64_t available = limit - block;
        size_t length = available > (int64_t)(XX_EWF2_SCAN_BLOCK + XX_EWF2_DESCRIPTOR_SIZE) ? (size_t)(XX_EWF2_SCAN_BLOCK + XX_EWF2_DESCRIPTOR_SIZE) : (size_t)available;
        size_t offset;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_ewf2_read_at(device, block, buffer, length)) return false;
        for (offset = 0U; offset + XX_EWF2_DESCRIPTOR_SIZE <= length && offset < XX_EWF2_SCAN_BLOCK; offset += 16U) {
            const uint8_t *raw = buffer + offset;
            uint64_t found_previous = 0U;
            if (xx_data_get_u32(raw, XX_EWF2_DESCRIPTOR_SIZE, 24U, false) != XX_EWF2_DESCRIPTOR_SIZE ||
                xx_data_get_u64(raw, XX_EWF2_DESCRIPTOR_SIZE, 8U, false) != previous) {
                continue;
            }
            if (xx_ewf2_parse_descriptor(raw, parsed->base_address, block + (int64_t)offset, out, &found_previous) && found_previous == previous &&
                out->data_at == from) {
                return true;
            }
        }
        block += XX_EWF2_SCAN_BLOCK;
    }
    return false;
}

/* The segment does not end the device: find the chain front to back. */
static bool xx_ewf2_chain_forward(xx_io_device *device, xx_ewf2_private *parsed, bool probe, xx_pd_struct *pd)
{
    uint8_t *buffer;
    int64_t from = parsed->base_address + XX_EWF2_HEADER_SIZE;
    uint64_t previous = 0U;
    bool result = false;

    buffer = (uint8_t *)xx_mem_alloc(XX_EWF2_SCAN_BLOCK + XX_EWF2_DESCRIPTOR_SIZE);
    if (!buffer) return false;
    parsed->section_count = 0U;
    for (;;) {
        xx_ewf2_section section;
        int64_t limit = parsed->input_size;

        if (probe && from <= parsed->input_size - XX_EWF2_PROBE_SCAN) {
            limit = from + XX_EWF2_PROBE_SCAN;
        }
        if (!xx_ewf2_scan_descriptor(device, parsed, from, previous, limit, buffer, &section, pd)) {
            break;
        }
        if (probe) {
            result = true;
            break;
        }
        if (!xx_ewf2_push(parsed, &section)) break;
        if (xx_ewf2_is_terminal(section.type)) {
            parsed->format_end = section.desc_at + XX_EWF2_DESCRIPTOR_SIZE;
            result = true;
            break;
        }
        previous = (uint64_t)(section.desc_at - parsed->base_address);
        from = section.desc_at + XX_EWF2_DESCRIPTOR_SIZE;
    }
    xx_mem_free(buffer);
    return result;
}

/* ------------------------------------------------------------- metadata -- */

/* Unpack one compressed metadata string and flatten its UTF-16 to ASCII
 * (every value the reader uses is a decimal number). */
static bool xx_ewf2_read_meta_text(xx_io_device *device, const xx_ewf2_private *parsed, const xx_ewf2_section *section, char **text_out, size_t *length_out)
{
    uint64_t packed_size = section->data_size - section->padding_size;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    char *text = NULL;
    size_t written = 0U;
    size_t index;
    size_t units;
    bool big_endian = false;
    size_t skip = 0U;
    bool result = false;

    if ((section->flags & XX_EWF2_FLAG_ENCRYPTED) != 0U || packed_size == 0U || packed_size > XX_EWF2_MAX_META_PACKED) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    plain = (uint8_t *)xx_mem_alloc(XX_EWF2_MAX_META_TEXT);
    if (!packed || !plain || !xx_ewf2_read_at(device, section->data_at, packed, (size_t)packed_size)) {
        goto done;
    }
    if (parsed->method == 1U) {
        if (!xx_zlib_stream_header_is_valid(packed, (size_t)packed_size) ||
            !xx_zlib_stream_decode_memory(packed, (size_t)packed_size, plain, XX_EWF2_MAX_META_TEXT, &written)) {
            goto done;
        }
    } else if (parsed->method == 2U) {
        if (!xx_bzip2_decompress_memory(packed, (size_t)packed_size, plain, XX_EWF2_MAX_META_TEXT, &written)) {
            goto done;
        }
    } else {
        xx_rt_memcpy(plain, packed, (size_t)packed_size);
        written = (size_t)packed_size;
    }
    if (written > XX_EWF2_MAX_META_TEXT || written < 2U) goto done;
    if (plain[0] == 0xFFU && plain[1] == 0xFEU) {
        skip = 2U;
    } else if (plain[0] == 0xFEU && plain[1] == 0xFFU) {
        skip = 2U;
        big_endian = true;
    }
    units = (written - skip) / 2U;
    text = (char *)xx_mem_alloc(units + 1U);
    if (!text) goto done;
    for (index = 0U; index < units; ++index) {
        uint16_t unit = xx_data_get_u16(plain + skip, written - skip, index * 2U, big_endian);
        text[index] = unit < 0x80U ? (char)unit : '?';
    }
    text[units] = '\0';
    *text_out = text;
    *length_out = units;
    text = NULL;
    result = true;
done:
    if (text) xx_mem_free(text);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

static bool xx_ewf2_parse_decimal(const char *text, size_t length, uint64_t *value)
{
    uint64_t result = 0U;
    size_t index;

    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint64_t digit;
        if (text[index] < '0' || text[index] > '9') return false;
        digit = (uint64_t)(text[index] - '0');
        if (result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *value = result;
    return true;
}

/* Lines 3 and 4 of the object string: tab-separated tags and values. */
static void xx_ewf2_parse_meta(const char *text, size_t length, xx_ewf2_meta *meta, bool device)
{
    size_t line_start[4];
    size_t line_end[4];
    size_t line = 0U;
    size_t position = 0U;
    size_t tag_at;
    size_t value_at;

    while (line < 4U && position <= length) {
        size_t end = position;
        while (end < length && text[end] != '\n') ++end;
        line_start[line] = position;
        line_end[line] = end;
        if (end > position && text[end - 1U] == '\r') --line_end[line];
        ++line;
        position = end + 1U;
    }
    if (line < 4U) return;
    tag_at = line_start[2];
    value_at = line_start[3];
    while (tag_at <= line_end[2]) {
        size_t tag_end = tag_at;
        size_t value_end = value_at;
        size_t tag_length;
        uint64_t number = 0U;
        bool has_number;

        while (tag_end < line_end[2] && text[tag_end] != '\t') ++tag_end;
        while (value_end < line_end[3] && text[value_end] != '\t') ++value_end;
        tag_length = tag_end - tag_at;
        has_number = value_at <= line_end[3] && xx_ewf2_parse_decimal(text + value_at, value_end - value_at, &number);
        if (has_number && tag_length == 2U) {
            const char *tag = text + tag_at;
            if (device && tag[0] == 't' && tag[1] == 's') {
                meta->sectors = number;
                meta->has_sectors = true;
            } else if (device && tag[0] == 'b' && tag[1] == 'p') {
                meta->bytes_per_sector = number;
                meta->has_bytes_per_sector = true;
            } else if (!device && tag[0] == 's' && tag[1] == 'b') {
                meta->sectors_per_chunk = number;
                meta->has_sectors_per_chunk = true;
            } else if (!device && tag[0] == 't' && tag[1] == 'b') {
                meta->chunks = number;
                meta->has_chunks = true;
            }
        }
        if (tag_end >= line_end[2]) break;
        tag_at = tag_end + 1U;
        value_at = value_end < line_end[3] ? value_end + 1U : line_end[3] + 1U;
    }
}

static void xx_ewf2_apply_geometry(xx_ewf2_private *parsed, const xx_ewf2_meta *meta)
{
    uint64_t bps = 512U;
    uint64_t spc;
    uint64_t sectors;
    uint64_t chunks;

    parsed->has_geometry = false;
    if (meta->has_bytes_per_sector && meta->bytes_per_sector != 0U) {
        bps = meta->bytes_per_sector;
    }
    if (!meta->has_sectors_per_chunk || meta->sectors_per_chunk == 0U || bps > XX_EWF2_MAX_BYTES_PER_SECTOR || meta->sectors_per_chunk > XX_EWF2_MAX_CHUNK_SIZE / bps) {
        return;
    }
    spc = meta->sectors_per_chunk;
    if (meta->has_sectors) {
        sectors = meta->sectors;
    } else if (meta->has_chunks && meta->chunks <= UINT64_MAX / spc) {
        sectors = meta->chunks * spc;
    } else {
        return;
    }
    if (sectors > (uint64_t)INT64_MAX / bps) return;
    chunks = sectors / spc + (sectors % spc != 0U ? 1U : 0U);
    /* The two counts are redundant; an image where they disagree cannot be
     * laid out with confidence. */
    if (meta->has_chunks && meta->has_sectors && meta->chunks != chunks) return;
    parsed->bytes_per_sector = (uint32_t)bps;
    parsed->sectors_per_chunk = (uint32_t)spc;
    parsed->chunk_size = (uint32_t)(spc * bps);
    parsed->number_of_sectors = sectors;
    parsed->number_of_chunks = chunks;
    parsed->media_size = sectors * bps;
    parsed->has_geometry = true;
}

static void xx_ewf2_read_hash(xx_io_device *device, xx_ewf2_private *parsed, const xx_ewf2_section *section)
{
    uint8_t data[XX_EWF2_SHA1_SECTION_SIZE];
    size_t digest = section->type == XX_EWF2_TYPE_MD5 ? 16U : 20U;

    if ((section->flags & XX_EWF2_FLAG_ENCRYPTED) != 0U || section->data_size < digest + 4U || !xx_ewf2_read_at(device, section->data_at, data, digest + 4U) ||
        xx_adler32(data, digest) != xx_data_get_u32(data, sizeof(data), digest, false) || xx_ewf2_is_zero(data, digest)) {
        return;
    }
    if (digest == 16U) {
        xx_rt_memcpy(parsed->md5, data, 16U);
        parsed->has_md5 = true;
    } else {
        xx_rt_memcpy(parsed->sha1, data, 20U);
        parsed->has_sha1 = true;
    }
}

/* The fixed part of a sector table. */
typedef struct xx_ewf2_table_s {
    uint64_t first_chunk;
    uint32_t entries;
    int64_t entries_at;
} xx_ewf2_table;

static bool xx_ewf2_read_table_header(xx_io_device *device, const xx_ewf2_section *section, xx_ewf2_table *table)
{
    uint8_t header[XX_EWF2_TABLE_HEADER_SIZE];
    uint64_t needed;

    if ((section->flags & XX_EWF2_FLAG_ENCRYPTED) != 0U || section->data_size < XX_EWF2_TABLE_HEADER_SIZE + XX_EWF2_TABLE_FOOTER_SIZE ||
        !xx_ewf2_read_at(device, section->data_at, header, sizeof(header)) || xx_adler32(header, 16U) != xx_data_get_u32(header, sizeof(header), 16U, false)) {
        return false;
    }
    table->first_chunk = xx_data_get_u64(header, sizeof(header), 0U, false);
    table->entries = xx_data_get_u32(header, sizeof(header), 8U, false);
    if (table->entries > XX_EWF2_MAX_TABLE_ENTRIES) return false;
    needed = XX_EWF2_TABLE_HEADER_SIZE + (uint64_t)table->entries * XX_EWF2_TABLE_ENTRY_SIZE + XX_EWF2_TABLE_FOOTER_SIZE;
    if (needed > section->data_size) return false;
    table->entries_at = section->data_at + (int64_t)XX_EWF2_TABLE_HEADER_SIZE;
    return true;
}

/* ------------------------------------------------------------- decoding -- */

static bool xx_ewf2_decode_chunk(xx_io_device *device, const xx_ewf2_private *parsed, xx_ewf2_decoder *decoder, const uint8_t *entry, int64_t limit, size_t expected,
                                 xx_pd_struct *pd)
{
    uint64_t offset = xx_data_get_u64(entry, XX_EWF2_TABLE_ENTRY_SIZE, 0U, false);
    uint32_t size = xx_data_get_u32(entry, XX_EWF2_TABLE_ENTRY_SIZE, 8U, false);
    uint32_t flags = xx_data_get_u32(entry, XX_EWF2_TABLE_ENTRY_SIZE, 12U, false);
    int64_t at;
    size_t written = 0U;

    if ((flags & XX_EWF2_CHUNK_COMPRESSED) != 0U && (flags & XX_EWF2_CHUNK_PATTERN_FILL) != 0U) {
        /* The offset field is the 8-byte pattern that fills the chunk. */
        size_t index;
        for (index = 0U; index < expected; ++index) {
            decoder->chunk[index] = entry[index % 8U];
        }
        return true;
    }
    /* A chunk lies in the segment, after the header and before the table
     * that lists it. */
    if (size == 0U || offset < XX_EWF2_HEADER_SIZE || offset > (uint64_t)(limit - parsed->base_address) ||
        (uint64_t)size > (uint64_t)(limit - parsed->base_address) - offset) {
        return false;
    }
    at = parsed->base_address + (int64_t)offset;
    if ((flags & XX_EWF2_CHUNK_COMPRESSED) != 0U) {
        if ((size_t)size > decoder->packed_capacity || !xx_ewf2_read_at(device, at, decoder->packed, size)) {
            return false;
        }
        if (parsed->method == 1U) {
            size_t consumed = 0U;
            int64_t produced;
            size_t trailer;
            if (size < 6U || !xx_zlib_stream_header_is_valid(decoder->packed, size) || xx_io_seek64(decoder->chunk_device, 0, SEEK_SET) != 0 ||
                !xx_deflate_unpack_memory_to_device_ex(decoder->packed + 2, (size_t)size - 2U, decoder->chunk_device, &consumed, false, pd)) {
                return false;
            }
            produced = xx_io_tell(decoder->chunk_device);
            if (produced < 0 || (uint64_t)produced < (uint64_t)expected) {
                return false;
            }
            /* The zlib trailer is the only check a compressed chunk has. */
            trailer = 2U + consumed;
            if (trailer > (size_t)size || (size_t)size - trailer < 4U) {
                return false;
            }
            return xx_data_get_u32(decoder->packed, size, trailer, true) == xx_adler32(decoder->chunk, (size_t)produced);
        }
        if (parsed->method == 2U) {
            return xx_bzip2_decompress_memory(decoder->packed, size, decoder->chunk, parsed->chunk_size, &written) && written >= expected &&
                   written <= parsed->chunk_size;
        }
        return false;
    }
    if ((flags & XX_EWF2_CHUNK_CHECKSUMMED) != 0U) {
        size_t stored;
        if (size < 4U) return false;
        stored = (size_t)size - 4U;
        if (stored < expected || stored > parsed->chunk_size || !xx_ewf2_read_at(device, at, decoder->chunk, stored + 4U)) {
            return false;
        }
        return xx_adler32(decoder->chunk, stored) == xx_data_get_u32(decoder->chunk, stored + 4U, stored, false);
    }
    if ((size_t)size < expected || size > parsed->chunk_size) return false;
    return xx_ewf2_read_at(device, at, decoder->chunk, expected);
}

static bool xx_ewf2_decode_table(xx_io_device *device, const xx_ewf2_private *parsed, xx_ewf2_decoder *decoder, const xx_ewf2_section *section, xx_pd_struct *pd)
{
    xx_ewf2_table table;
    uint8_t footer[4];
    uint32_t checksum = 1U;
    uint32_t done;

    if (!xx_ewf2_read_table_header(device, section, &table) || table.first_chunk != decoder->next_chunk) {
        return false;
    }
    /* First the entry array's checksum, so nothing of a damaged table is
     * written. */
    for (done = 0U; done < table.entries;) {
        uint32_t batch = table.entries - done;
        if (batch > XX_EWF2_ENTRY_BATCH) batch = XX_EWF2_ENTRY_BATCH;
        if (!xx_ewf2_read_at(device, table.entries_at + (int64_t)done * XX_EWF2_TABLE_ENTRY_SIZE, decoder->entries, (size_t)batch * XX_EWF2_TABLE_ENTRY_SIZE)) {
            return false;
        }
        checksum = xx_adler32_update(checksum, decoder->entries, (size_t)batch * XX_EWF2_TABLE_ENTRY_SIZE);
        done += batch;
    }
    if (!xx_ewf2_read_at(device, table.entries_at + (int64_t)table.entries * XX_EWF2_TABLE_ENTRY_SIZE, footer, sizeof(footer)) ||
        xx_data_get_u32(footer, sizeof(footer), 0U, false) != checksum) {
        return false;
    }
    for (done = 0U; done < table.entries;) {
        uint32_t batch = table.entries - done;
        uint32_t index;
        if (batch > XX_EWF2_ENTRY_BATCH) batch = XX_EWF2_ENTRY_BATCH;
        if (!xx_ewf2_read_at(device, table.entries_at + (int64_t)done * XX_EWF2_TABLE_ENTRY_SIZE, decoder->entries, (size_t)batch * XX_EWF2_TABLE_ENTRY_SIZE)) {
            return false;
        }
        for (index = 0U; index < batch; ++index) {
            uint64_t left;
            size_t expected;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (decoder->next_chunk >= parsed->number_of_chunks) return false;
            left = parsed->media_size - decoder->next_chunk * (uint64_t)parsed->chunk_size;
            expected = left < (uint64_t)parsed->chunk_size ? (size_t)left : (size_t)parsed->chunk_size;
            if (!xx_ewf2_decode_chunk(device, parsed, decoder, decoder->entries + (size_t)index * XX_EWF2_TABLE_ENTRY_SIZE, section->data_at, expected, pd)) {
                return false;
            }
            xx_hash_update(&decoder->md5, decoder->chunk, expected);
            xx_hash_update(&decoder->sha1, decoder->chunk, expected);
            if (decoder->output && !xx_ewf2_write_all(decoder->output, decoder->chunk, expected)) {
                return false;
            }
            ++decoder->next_chunk;
        }
        done += batch;
    }
    return true;
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_ewf2_info_pass(xx_io_device *device, xx_ewf2_private *parsed, xx_pd_struct *pd)
{
    xx_ewf2_meta meta;
    bool have_device = false;
    bool have_case = false;
    uint32_t index;

    xx_mem_zero(&meta, sizeof(meta));
    for (index = 0U; index < parsed->section_count; ++index) {
        const xx_ewf2_section *section = &parsed->sections[index];

        if (pd && xx_pd_is_stopped(pd)) return false;
        if ((section->flags & XX_EWF2_FLAG_ENCRYPTED) != 0U) {
            parsed->encrypted = true;
        }
        switch (section->type) {
            case XX_EWF2_TYPE_DEVICE_INFORMATION:
            case XX_EWF2_TYPE_CASE_DATA: {
                bool is_device = section->type == XX_EWF2_TYPE_DEVICE_INFORMATION;
                char *text = NULL;
                size_t length = 0U;
                if (is_device ? have_device : have_case) break;
                if (is_device) have_device = true;
                else have_case = true;
                if (xx_ewf2_read_meta_text(device, parsed, section, &text, &length)) {
                    xx_ewf2_parse_meta(text, length, &meta, is_device);
                    xx_mem_free(text);
                }
                break;
            }
            case XX_EWF2_TYPE_SECTOR_DATA:
                if (parsed->first_data < 0) parsed->first_data = section->data_at;
                break;
            case XX_EWF2_TYPE_SECTOR_TABLE: {
                xx_ewf2_table table;
                if (!xx_ewf2_read_table_header(device, section, &table)) {
                    parsed->contiguous = false;
                    break;
                }
                if (table.first_chunk != parsed->table_entries) {
                    parsed->contiguous = false;
                }
                parsed->table_entries += table.entries;
                if (parsed->first_data < 0) parsed->first_data = section->data_at;
                break;
            }
            case XX_EWF2_TYPE_MD5:
            case XX_EWF2_TYPE_SHA1: xx_ewf2_read_hash(device, parsed, section); break;
            case XX_EWF2_TYPE_ENCRYPTION_KEYS: parsed->encrypted = true; break;
            case XX_EWF2_TYPE_DONE: parsed->last_segment = true; break;
            default: break;
        }
    }
    xx_ewf2_apply_geometry(parsed, &meta);
    parsed->complete = parsed->has_geometry && !parsed->encrypted && parsed->contiguous && parsed->table_entries == parsed->number_of_chunks;
    return true;
}

static bool xx_ewf2_parse(Abstractformat *self, xx_ewf2_private *parsed, bool probe, xx_pd_struct *pd)
{
    if (parsed) xx_ewf2_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    parsed->base_address = self->base_address;
    if (!xx_ewf2_read_header(self->device, parsed)) return false;
    if (!xx_ewf2_chain_backward(self->device, parsed, probe, pd) && !xx_ewf2_chain_forward(self->device, parsed, probe, pd)) {
        return false;
    }
    if (probe) return true;
    return xx_ewf2_info_pass(self->device, parsed, pd);
}

static bool xx_ewf2_write_image(Abstractformat *self, xx_io_device *output, xx_pd_struct *pd)
{
    xx_ewf2_private parsed;
    xx_ewf2_decoder decoder;
    uint8_t md5[16];
    uint8_t sha1[20];
    uint32_t index;
    bool hashing = false;
    bool result = false;

    xx_mem_zero(&parsed, sizeof(parsed));
    xx_mem_zero(&decoder, sizeof(decoder));
    if (!xx_ewf2_parse(self, &parsed, false, pd) || !parsed.complete) {
        goto done;
    }
    decoder.output = output;
    decoder.packed_capacity = (size_t)parsed.chunk_size * 2U + 1024U;
    decoder.chunk = (uint8_t *)xx_mem_alloc((size_t)parsed.chunk_size + 4U);
    decoder.packed = (uint8_t *)xx_mem_alloc(decoder.packed_capacity);
    decoder.entries = (uint8_t *)xx_mem_alloc((size_t)XX_EWF2_ENTRY_BATCH * XX_EWF2_TABLE_ENTRY_SIZE);
    if (!decoder.chunk || !decoder.packed || !decoder.entries || !xx_hash_init(&decoder.md5, XX_HASH_MD5) || !xx_hash_init(&decoder.sha1, XX_HASH_SHA1)) {
        goto done;
    }
    hashing = true;
    decoder.chunk_device = xx_io_mem_open(decoder.chunk, parsed.chunk_size);
    if (!decoder.chunk_device) goto done;
    for (index = 0U; index < parsed.section_count; ++index) {
        if (parsed.sections[index].type == XX_EWF2_TYPE_SECTOR_TABLE && !xx_ewf2_decode_table(self->device, &parsed, &decoder, &parsed.sections[index], pd)) {
            goto done;
        }
    }
    if (decoder.next_chunk != parsed.number_of_chunks) goto done;
    hashing = false;
    if (!xx_hash_final(&decoder.md5, md5, sizeof(md5)) || !xx_hash_final(&decoder.sha1, sha1, sizeof(sha1))) {
        goto done;
    }
    result = (!parsed.has_md5 || xx_rt_memcmp(md5, parsed.md5, sizeof(md5)) == 0) && (!parsed.has_sha1 || xx_rt_memcmp(sha1, parsed.sha1, sizeof(sha1)) == 0);
done:
    if (hashing) {
        (void)xx_hash_final(&decoder.md5, md5, sizeof(md5));
        (void)xx_hash_final(&decoder.sha1, sha1, sizeof(sha1));
    }
    if (decoder.chunk_device) xx_io_close(decoder.chunk_device);
    if (decoder.chunk) xx_mem_free(decoder.chunk);
    if (decoder.packed) xx_mem_free(decoder.packed);
    if (decoder.entries) xx_mem_free(decoder.entries);
    if (parsed.sections) xx_mem_free(parsed.sections);
    return result;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_ewf2_ex01_init(xx_ewf2_ex01 *ewf, xx_io_device *dev, int64_t base_address)
{
    if (!ewf) return;
    xx_mem_zero(ewf, sizeof(*ewf));
    xx_format_init(&ewf->format, dev, base_address);
    ewf->format.endian = XX_ENDIAN_LITTLE;
    ewf->format.file_type = XX_EWF2_EX01_FILE_TYPE;
    ewf->format.format_type = XX_TYPE_ARCHIVE;
    ewf->format.is_archive = true;
    xx_format_set_mime_type(&ewf->format, "application/x-ewf2");
    xx_format_set_extension(&ewf->format, "Ex01");
    ewf->format.check_is_valid = xx_ewf2_ex01_check_is_valid;
    ewf->format.handle_base_info = xx_ewf2_ex01_handle_base_info;
    ewf->format.get_format_size = xx_ewf2_ex01_get_format_size;
    ewf->format.get_number_of_archive_records = xx_ewf2_ex01_get_number_of_archive_records;
    ewf->format.create_archive_records_reading = xx_ewf2_ex01_create_archive_records_reading;
    ewf->format.get_current_archive_record = xx_ewf2_ex01_get_current_archive_record;
    ewf->format.unpack_current_archive_record = xx_ewf2_ex01_unpack_current_archive_record;
    ewf->format.archive_record_move_to_next = xx_ewf2_ex01_archive_record_move_to_next;
    ewf->format.free_archive_records_reading = xx_ewf2_ex01_free_archive_records_reading;
    ewf->format.destroy = xx_ewf2_vtable_destroy;
}

xx_ewf2_ex01 *xx_ewf2_ex01_create(xx_io_device *dev, int64_t base_address)
{
    xx_ewf2_ex01 *ewf = (xx_ewf2_ex01 *)xx_mem_alloc(sizeof(*ewf));

    if (ewf) xx_ewf2_ex01_init(ewf, dev, base_address);
    return ewf;
}

void xx_ewf2_ex01_destroy(xx_ewf2_ex01 *ewf)
{
    if (!ewf) return;
    if (ewf->internal) {
        xx_ewf2_private_free(ewf->internal);
        ewf->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&ewf->format);
}

static void xx_ewf2_vtable_destroy(Abstractformat *self)
{
    xx_ewf2_ex01_destroy((xx_ewf2_ex01 *)self);
}

void xx_ewf2_ex01_free(xx_ewf2_ex01 *ewf)
{
    if (!ewf) return;
    xx_ewf2_ex01_destroy(ewf);
    xx_mem_free(ewf);
}

/* --------------------------------------------------------------- format -- */

bool xx_ewf2_ex01_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_ewf2_private parsed;
    bool result;

    xx_mem_zero(&parsed, sizeof(parsed));
    result = xx_ewf2_parse(self, &parsed, true, pd);
    if (parsed.sections) xx_mem_free(parsed.sections);
    return result;
}

bool xx_ewf2_ex01_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_ewf2_ex01 *ewf = (xx_ewf2_ex01 *)self;
    xx_ewf2_private *parsed;

    if (!self) return false;
    parsed = (xx_ewf2_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!parsed || !xx_ewf2_parse(self, parsed, false, pd)) {
        if (parsed) xx_ewf2_private_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    /* The section list is not needed after this. */
    if (parsed->sections) {
        xx_mem_free(parsed->sections);
        parsed->sections = NULL;
        parsed->section_capacity = 0U;
    }
    if (ewf->internal) xx_ewf2_private_free(ewf->internal);
    ewf->internal = parsed;
    ewf->number_of_records = 1U;
    ewf->media_size = parsed->media_size;
    ewf->number_of_sectors = parsed->number_of_sectors;
    ewf->number_of_chunks = parsed->number_of_chunks;
    ewf->table_entries = parsed->table_entries;
    ewf->sectors_per_chunk = parsed->sectors_per_chunk;
    ewf->bytes_per_sector = parsed->bytes_per_sector;
    ewf->chunk_size = parsed->chunk_size;
    ewf->segment_number = parsed->segment_number;
    ewf->compression_method = parsed->method;
    ewf->minor_version = parsed->minor_version;
    ewf->has_geometry = parsed->has_geometry;
    ewf->is_encrypted = parsed->encrypted;
    ewf->is_last_segment = parsed->last_segment;
    ewf->is_complete = parsed->complete;
    ewf->has_md5 = parsed->has_md5;
    ewf->has_sha1 = parsed->has_sha1;
    xx_rt_memcpy(ewf->md5, parsed->md5, sizeof(ewf->md5));
    xx_rt_memcpy(ewf->sha1, parsed->sha1, sizeof(ewf->sha1));
    xx_rt_memcpy(ewf->set_identifier, parsed->guid, sizeof(ewf->set_identifier));
    self->format_size = parsed->format_end - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    if (parsed->format_end < parsed->input_size) {
        self->overlay_offset = parsed->format_end;
        self->overlay_size = parsed->input_size - parsed->format_end;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_ewf2_ex01_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_ewf2_ex01_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_ewf2_ex01 *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_ewf2_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;

    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ewf2_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_ewf2_populate_record(xx_archive_record *record, const xx_ewf2_private *parsed)
{
    char comment[112];
    char hex[48];
    size_t used = 0U;
    uint64_t packed = (uint64_t)(parsed->format_end - parsed->base_address);

    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->header_size = XX_EWF2_HEADER_SIZE;
    record->data_offset = parsed->first_data;
    record->compressed_size = (int64_t)packed;
    if (!xx_archive_record_set_original_name(record, XX_EWF2_MEMBER_NAME) || !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, parsed->media_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, packed) || !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, parsed->encrypted)) {
        return false;
    }
    comment[0] = '\0';
    if (parsed->has_md5 && xx_hash_to_hex(parsed->md5, 16U, hex, sizeof(hex))) {
        used = (size_t)xx_rt_snprintf(comment, sizeof(comment), "MD5 %s", hex);
    }
    if (parsed->has_sha1 && used < sizeof(comment) && xx_hash_to_hex(parsed->sha1, 20U, hex, sizeof(hex))) {
        (void)xx_rt_snprintf(comment + used, sizeof(comment) - used, "%sSHA1 %s", used ? "; " : "", hex);
    }
    if (comment[0] != '\0' && !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, comment)) {
        return false;
    }
    return true;
}

xx_archive_record_state *xx_ewf2_ex01_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_ewf2_private *parsed;

    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_ewf2_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = parsed;
    state->free_internal = xx_ewf2_private_free;
    if (!xx_ewf2_copy_options(&state->options, options) || !xx_ewf2_parse(self, parsed, false, pd)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    /* Extraction parses again; only the summary is kept here. */
    if (parsed->sections) {
        xx_mem_free(parsed->sections);
        parsed->sections = NULL;
        parsed->section_capacity = 0U;
    }
    state->total_records = 1;
    if (!xx_ewf2_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ewf2_ex01_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_ewf2_ex01_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_ewf2_private *parsed;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_ewf2_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_ewf2_ex01_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_ewf2_private *parsed;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result;
    bool created = false;

    if (!self || !self->device || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed = (xx_ewf2_private *)state->internal_state;
    if (!parsed || parsed->consumed || !parsed->complete) return false;

    option = xx_ewf2_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return xx_ewf2_write_image(self, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) {
        if (owned_base) xx_str_free(owned_base);
        return false;
    }
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", XX_EWF2_MEMBER_NAME);
    } else {
        destination = xx_str_concat(base, XX_EWF2_MEMBER_NAME);
    }
    if (owned_base) xx_str_free(owned_base);
    if (!destination) return false;
    if (!xx_store_create_dirs_a(destination, false)) {
        xx_str_free(destination);
        return false;
    }
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    result = output != NULL && xx_ewf2_write_image(self, output, pd);
    if (output && xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(destination);
    xx_str_free(destination);
    return result;
}

void xx_ewf2_ex01_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

bool xx_ewf2_ex01_unpack_to_device(xx_ewf2_ex01 *ewf, xx_io_device *output, xx_pd_struct *pd)
{
    if (!ewf || !ewf->format.device) return false;
    return xx_ewf2_write_image(&ewf->format, output, pd);
}

uint64_t xx_ewf2_ex01_get_media_size(const xx_ewf2_ex01 *ewf)
{
    return ewf ? ewf->media_size : 0U;
}
uint32_t xx_ewf2_ex01_get_chunk_size(const xx_ewf2_ex01 *ewf)
{
    return ewf ? ewf->chunk_size : 0U;
}
bool xx_ewf2_ex01_is_complete(const xx_ewf2_ex01 *ewf)
{
    return ewf ? ewf->is_complete : false;
}
