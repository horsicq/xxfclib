/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <xxfclib/formats/archive/zip/xx_zip_yx.h>

#include <xxfclib/algo/protobuf/xx_protobuf_wire.h>
#include <xxfclib/data/xx_data.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define XXZIP_EOCD 0x06054b50u
#define XXZIP_CENTRAL 0x02014b50u
#define XXZIP_ZIP64_EOCD 0x06064b50u
#define XXZIP_ZIP64_LOCATOR 0x07064b50u
#define XXZIP_MAX_ENTRIES 100000u

typedef struct {
    uint64_t entry_count;
    uint64_t cd_size;
    uint64_t cd_offset;
    uint64_t cd_absolute;
    uint64_t base_offset;
    uint64_t directory_end;
} xxzip_archive;

static uint16_t xxzip_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}

static uint32_t xxzip_u32(const uint8_t *p)
{
    return (uint32_t)xxzip_u16(p) | (uint32_t)xxzip_u16(p + 2) << 16;
}

static uint64_t xxzip_u64(const uint8_t *p)
{
    return (uint64_t)xxzip_u32(p) | (uint64_t)xxzip_u32(p + 4) << 32;
}

static bool xxzip_span(size_t size, uint64_t at, uint64_t length)
{
    return at <= size && length <= (uint64_t)size - at;
}

static bool xxzip_signature(const uint8_t *data, size_t size,
                            uint64_t at, uint32_t signature)
{
    return xxzip_span(size, at, 4) && xxzip_u32(data + (size_t)at) == signature;
}

static bool xxzip_find_eocd(const uint8_t *data, size_t size, uint64_t *offset)
{
    if (size < 22) return false;
    static const uint8_t signature[] = {'P', 'K', 5, 6};
    size_t start = size > 65557 ? size - 65557 : 0;
    size_t length = size - start;
    size_t scan = 0;
    bool found = false;
    while (scan <= length - 22) {
        int64_t position = xx_data_find_bytes_buffer_optimize(
            data + start, length, scan, signature, sizeof(signature), NULL);
        if (position < 0) break;
        scan = (size_t)position + 1;
        if ((size_t)position > length - 22) break;
        size_t comment = xxzip_u16(data + start + (size_t)position + 20);
        if (comment <= length - ((size_t)position + 22)) {
            *offset = (uint64_t)(start + (size_t)position);
            found = true;
        }
    }
    return found;
}

static bool xxzip_zip64_record(const uint8_t *data, size_t size,
                               uint64_t eocd_at, uint64_t *record_at,
                               uint64_t *count, uint64_t *cd_size,
                               uint64_t *cd_offset)
{
    if (eocd_at < 20) return false;
    uint64_t locator = eocd_at - 20;
    if (!xxzip_signature(data, size, locator, XXZIP_ZIP64_LOCATOR)) return false;
    const uint8_t *loc = data + (size_t)locator;
    if (xxzip_u32(loc + 4) != 0 || xxzip_u32(loc + 16) != 1) return false;
    uint64_t advertised = xxzip_u64(loc + 8);
    uint64_t record = UINT64_MAX;
    if (xxzip_signature(data, size, advertised, XXZIP_ZIP64_EOCD)) {
        record = advertised;
    } else {
        uint64_t start = locator > 4096 ? locator - 4096 : 0;
        for (uint64_t i = locator; i > start;) {
            --i;
            if (xxzip_signature(data, size, i, XXZIP_ZIP64_EOCD)) {
                record = i;
                break;
            }
        }
    }
    if (record == UINT64_MAX || !xxzip_span(size, record, 56)) return false;
    const uint8_t *header = data + (size_t)record;
    uint64_t record_size = xxzip_u64(header + 4);
    if (record_size > UINT64_MAX - 12 ||
        !xxzip_span(size, record, record_size + 12)) return false;
    if (xxzip_u32(header + 16) != 0 || xxzip_u32(header + 20) != 0)
        return false;
    *record_at = record;
    *count = xxzip_u64(header + 32);
    *cd_size = xxzip_u64(header + 40);
    *cd_offset = xxzip_u64(header + 48);
    return true;
}

static bool xxzip_open(const uint8_t *data, size_t size, xxzip_archive *archive)
{
    uint64_t eocd_at;
    if (!xxzip_find_eocd(data, size, &eocd_at)) return false;
    const uint8_t *eocd = data + (size_t)eocd_at;
    if (xxzip_u16(eocd + 4) != 0 || xxzip_u16(eocd + 6) != 0)
        return false;
    uint64_t count = xxzip_u16(eocd + 10);
    uint64_t cd_size = xxzip_u32(eocd + 12);
    uint64_t cd_offset = xxzip_u32(eocd + 16);
    uint64_t payload_end = eocd_at;
    if (count == UINT16_MAX || cd_size == UINT32_MAX ||
        cd_offset == UINT32_MAX) {
        if (!xxzip_zip64_record(data, size, eocd_at, &payload_end,
                                &count, &cd_size, &cd_offset))
            return false;
    }
    if (cd_offset > UINT64_MAX - cd_size ||
        cd_offset + cd_size > payload_end)
        return false;
    uint64_t inferred_base = payload_end - (cd_offset + cd_size);
    if (cd_offset > UINT64_MAX - inferred_base) return false;
    uint64_t inferred = inferred_base + cd_offset;
    uint64_t absolute = xxzip_signature(data, size, cd_offset, XXZIP_CENTRAL)
                        ? cd_offset : inferred;
    if (absolute < cd_offset || absolute > size ||
        cd_size > UINT64_MAX - absolute ||
        absolute + cd_size > eocd_at)
        return false;
    if (count == 0 && cd_size == 0 && cd_offset == 0 && eocd_at != 0)
        return false;
    archive->entry_count = count;
    archive->cd_size = cd_size;
    archive->cd_offset = cd_offset;
    archive->cd_absolute = absolute;
    archive->base_offset = absolute - cd_offset;
    archive->directory_end = payload_end;
    return true;
}

static bool xxzip_zip64_extra(const uint8_t *extra, size_t length,
                              bool need_uncompressed, bool need_compressed,
                              bool need_offset, uint64_t *uncompressed,
                              uint64_t *compressed, uint64_t *offset)
{
    size_t at = 0;
    while (length - at >= 4) {
        uint16_t kind = xxzip_u16(extra + at);
        size_t field_len = xxzip_u16(extra + at + 2);
        at += 4;
        if (field_len > length - at) return false;
        if (kind == 1) {
            size_t pos = at;
            size_t end = at + field_len;
            if (need_uncompressed) {
                if (end - pos < 8) return false;
                *uncompressed = xxzip_u64(extra + pos);
                pos += 8;
            }
            if (need_compressed) {
                if (end - pos < 8) return false;
                *compressed = xxzip_u64(extra + pos);
                pos += 8;
            }
            if (need_offset) {
                if (end - pos < 8) return false;
                *offset = xxzip_u64(extra + pos);
            }
            return true;
        }
        at += field_len;
    }
    return false;
}

/* Match Rust's String::from_utf8_lossy on each invalid byte sequence. */
static char *xxzip_utf8_lossy(const uint8_t *source, size_t length,
                              size_t *out_length)
{
    if (length > (SIZE_MAX - 1) / 3) return NULL;
    char *output = (char *)malloc(length * 3 + 1);
    if (!output) return NULL;
    size_t in = 0, out = 0;
    while (in < length) {
        uint8_t lead = source[in];
        if (lead < 0x80) {
            output[out++] = (char)lead;
            ++in;
            continue;
        }
        size_t width = lead >= 0xc2 && lead <= 0xdf ? 2 :
                       lead >= 0xe0 && lead <= 0xef ? 3 :
                       lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (width) {
            size_t valid = 1;
            while (valid < width && in + valid < length) {
                uint8_t byte = source[in + valid];
                if (byte < 0x80 || byte > 0xbf) break;
                if (valid == 1 &&
                    ((lead == 0xe0 && byte < 0xa0) ||
                     (lead == 0xed && byte > 0x9f) ||
                     (lead == 0xf0 && byte < 0x90) ||
                     (lead == 0xf4 && byte > 0x8f)))
                    break;
                ++valid;
            }
            if (valid == width) {
                memcpy(output + out, source + in, width);
                in += width;
                out += width;
                continue;
            }
            in += valid;
        } else {
            ++in;
        }
        output[out++] = (char)0xef;
        output[out++] = (char)0xbf;
        output[out++] = (char)0xbd;
    }
    output[out] = '\0';
    *out_length = out;
    return output;
}

static uint32_t xxzip_compression(uint16_t raw)
{
    switch (raw) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6:
    case 8: case 9: case 10: case 12: case 14: case 93: case 95:
        return raw;
    default:
        return 255;
    }
}

XXFC_API void xx_zip_yx_result_free(xx_zip_yx_result *result)
{
    if (!result) return;
    for (size_t i = 0; i < result->entry_count; ++i)
        free(result->entries[i].file_path);
    free(result->entries);
    memset(result, 0, sizeof(*result));
}

static bool xxzip_pb_varint(const uint8_t *data, size_t size, size_t *at,
                            uint64_t *value)
{
    return xx_protobuf_wire_varint(data, size, at, value);
}

static bool xxzip_pb_skip(const uint8_t *data, size_t size, size_t *at,
                          uint64_t field, unsigned wire, unsigned depth)
{
    return xx_protobuf_wire_skip_field_zip(data, size, at, field, wire,
                                           depth);
}

static bool xxzip_pb_entry(const uint8_t *data, size_t size,
                           xx_zip_yx_entry *entry)
{
    size_t at = 0;
    while (at < size) {
        uint64_t key, value;
        if (!xxzip_pb_varint(data, size, &at, &key)) return false;
        uint64_t field = key >> 3;
        unsigned wire = (unsigned)(key & 7);
        if (field == 1 && wire == 2) {
            if (!xxzip_pb_varint(data, size, &at, &value) ||
                value > size - at || value == SIZE_MAX) return false;
            char *name = (char *)malloc((size_t)value + 1);
            if (!name) return false;
            memcpy(name, data + at, (size_t)value);
            name[value] = '\0';
            free(entry->file_path);
            entry->file_path = name;
            entry->file_path_length = (size_t)value;
            entry->file_path_present = true;
            at += (size_t)value;
        } else if (wire == 0 && field >= 2 && field <= 4) {
            if (!xxzip_pb_varint(data, size, &at, &value)) return false;
            if (field == 2) {
                entry->compression = (uint32_t)value;
                entry->compression_present = true;
            } else if (field == 3) {
                entry->uncompressed_size = value;
                entry->uncompressed_size_present = true;
            } else {
                entry->compressed_size = value;
                entry->compressed_size_present = true;
            }
        } else if (!xxzip_pb_skip(data, size, &at, field, wire, 0)) {
            return false;
        }
    }
    return true;
}

XXFC_API bool xx_zip_yx_parse_output(const uint8_t *data, size_t size,
                                     xx_zip_yx_result *result)
{
    if (!result || (!data && size)) return false;
    memset(result, 0, sizeof(*result));
    size_t at = 0;
    size_t capacity = 0;
    while (at < size) {
        uint64_t key, value;
        if (!xxzip_pb_varint(data, size, &at, &key)) goto fail;
        uint64_t field = key >> 3;
        unsigned wire = (unsigned)(key & 7);
        if (field == 1 && wire == 0) {
            if (!xxzip_pb_varint(data, size, &at, &value)) goto fail;
            result->is_zip = value != 0;
            result->is_zip_present = true;
        } else if (field == 2 && wire == 2) {
            if (!xxzip_pb_varint(data, size, &at, &value) ||
                value > size - at) goto fail;
            if (result->entry_count == capacity) {
                size_t next = capacity ? capacity * 2 : 8;
                if (next < capacity || next > SIZE_MAX / sizeof(xx_zip_yx_entry))
                    goto fail;
                xx_zip_yx_entry *grown = (xx_zip_yx_entry *)realloc(
                    result->entries, next * sizeof(*grown));
                if (!grown) goto fail;
                result->entries = grown;
                capacity = next;
            }
            xx_zip_yx_entry *entry = &result->entries[result->entry_count];
            memset(entry, 0, sizeof(*entry));
            ++result->entry_count;
            if (!xxzip_pb_entry(data + at, (size_t)value, entry)) goto fail;
            at += (size_t)value;
        } else if (!xxzip_pb_skip(data, size, &at, field, wire, 0)) {
            goto fail;
        }
    }
    return true;
fail:
    xx_zip_yx_result_free(result);
    return false;
}

XXFC_API bool xx_zip_yx_parse(const uint8_t *data, size_t size,
                              xx_zip_yx_result *result)
{
    if (!result || (!data && size)) return false;
    memset(result, 0, sizeof(*result));
    result->is_zip_present = true;
    if (!data || size < 22) return true;
    xxzip_archive archive = {0};
    if (!xxzip_open(data, size, &archive)) return true;
    result->is_zip = true;
    uint64_t at = archive.cd_absolute;
    size_t capacity = 0;
    for (uint64_t i = 0; i < archive.entry_count &&
                         result->entry_count < XXZIP_MAX_ENTRIES; ++i) {
        if (!xxzip_span(size, at, 46) ||
            !xxzip_signature(data, size, at, XXZIP_CENTRAL)) break;
        const uint8_t *header = data + (size_t)at;
        uint16_t flags = xxzip_u16(header + 8);
        if (flags & ((1u << 6) | (1u << 13))) break;
        uint64_t name_len = xxzip_u16(header + 28);
        uint64_t extra_len = xxzip_u16(header + 30);
        uint64_t comment_len = xxzip_u16(header + 32);
        uint64_t record_len = 46 + name_len + extra_len + comment_len;
        if (at > UINT64_MAX - record_len ||
            at + record_len > archive.directory_end ||
            !xxzip_span(size, at, record_len)) break;

        uint64_t compressed = xxzip_u32(header + 20);
        uint64_t uncompressed = xxzip_u32(header + 24);
        uint64_t local_offset = xxzip_u32(header + 42);
        bool need_compressed = compressed == UINT32_MAX;
        bool need_uncompressed = uncompressed == UINT32_MAX;
        bool need_offset = local_offset == UINT32_MAX;
        if (need_compressed || need_uncompressed || need_offset) {
            if (extra_len > 256 ||
                !xxzip_zip64_extra(data + (size_t)(at + 46 + name_len),
                                   (size_t)extra_len, need_uncompressed,
                                   need_compressed, need_offset,
                                   &uncompressed, &compressed, &local_offset))
                break;
        }
        (void)local_offset;
        if (result->entry_count == capacity) {
            size_t next = capacity ? capacity * 2 : 8;
            if (next > XXZIP_MAX_ENTRIES) next = XXZIP_MAX_ENTRIES;
            xx_zip_yx_entry *grown = (xx_zip_yx_entry *)realloc(
                result->entries, next * sizeof(*grown));
            if (!grown) {
                xx_zip_yx_result_free(result);
                return false;
            }
            result->entries = grown;
            capacity = next;
        }
        xx_zip_yx_entry *entry = &result->entries[result->entry_count];
        memset(entry, 0, sizeof(*entry));
        entry->file_path = xxzip_utf8_lossy(data + (size_t)(at + 46),
                                            (size_t)name_len,
                                            &entry->file_path_length);
        if (!entry->file_path) {
            xx_zip_yx_result_free(result);
            return false;
        }
        entry->compression = xxzip_compression(xxzip_u16(header + 10));
        entry->uncompressed_size = uncompressed;
        entry->compressed_size = compressed;
        entry->file_path_present = true;
        entry->compression_present = true;
        entry->uncompressed_size_present = true;
        entry->compressed_size_present = true;
        ++result->entry_count;
        at += record_len;
    }
    return true;
}

XXFC_API bool xx_zip_yx_constant(const char *member, uint64_t *value)
{
    if (!member || !value) return false;
    if (strncmp(member, "zip.", 4) == 0) member += 4;
    if (strncmp(member, "Compression.", 12) == 0) member += 12;
    static const struct { const char *name; uint64_t value; } constants[] = {
        {"STORED", 0}, {"SHRUNK", 1}, {"REDUCED_1", 2},
        {"REDUCED_2", 3}, {"REDUCED_3", 4}, {"REDUCED_4", 5},
        {"IMPLODED", 6}, {"DEFLATED", 8}, {"DEFLATE64", 9},
        {"PKWARE_IMPLODING", 10}, {"BZIP2", 12}, {"LZMA", 14},
        {"ZSTD", 93}, {"XZ", 95}, {"UNKNOWN", 255}
    };
    for (size_t i = 0; i < sizeof(constants) / sizeof(constants[0]); ++i) {
        if (strcmp(member, constants[i].name) == 0) {
            *value = constants[i].value;
            return true;
        }
    }
    return false;
}
