/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* UDF (ECMA-167 volume/file structure with the OSTA UDF constraints) reader.
 *
 * The walk is: Anchor Volume Descriptor Pointer -> main Volume Descriptor
 * Sequence -> Partition Descriptor(s) + Logical Volume Descriptor -> File Set
 * Descriptor -> root File Entry -> File Identifier Descriptors.  Everything a
 * descriptor says about sizes and locations is attacker controlled, so every
 * extent is bounds checked against the device, every list is length capped and
 * the directory walk keeps a visited set of File Entry offsets so a cyclic ICB
 * reference terminates.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/udf/xx_udf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* A UDF image recorded in a file uses 2048 byte logical sectors; the Volume
 * Recognition Sequence at 32768 is defined in terms of them and OSTA UDF
 * requires the logical block size to equal the logical sector size. */
#define XX_UDF_SECTOR_SIZE 2048U
#define XX_UDF_TAG_SIZE 16U
#define XX_UDF_VRS_OFFSET 32768
#define XX_UDF_VRS_MAX_DESCRIPTORS 64U

#define XX_UDF_MAX_VDS_BLOCKS 256U
#define XX_UDF_MAX_PARTITIONS 16U
#define XX_UDF_MAX_PARTITION_MAPS 16U
#define XX_UDF_MAX_ENTRIES 100000U
#define XX_UDF_MAX_VISITED 8192U
#define XX_UDF_MAX_DEPTH 64U
#define XX_UDF_MAX_NAME_SIZE 4096U
#define XX_UDF_MAX_NAME_UTF8 768U /* 255 CS0 bytes expand to at most 762. */
#define XX_UDF_MAX_EXTENTS 1024U
#define XX_UDF_MAX_DIR_SIZE 0x4000000 /* 64 MiB of File Identifier Descriptors. */

/* ECMA-167 4/7.2.1 tag identifiers (only the ones this reader acts on). */
#define XX_UDF_TAG_PRIMARY_VOLUME_DESCRIPTOR 1U
#define XX_UDF_TAG_ANCHOR_VOLUME_DESCRIPTOR_POINTER 2U
#define XX_UDF_TAG_PARTITION_DESCRIPTOR 5U
#define XX_UDF_TAG_LOGICAL_VOLUME_DESCRIPTOR 6U
#define XX_UDF_TAG_TERMINATING_DESCRIPTOR 8U
#define XX_UDF_TAG_FILE_SET_DESCRIPTOR 256U
#define XX_UDF_TAG_FILE_IDENTIFIER_DESCRIPTOR 257U
#define XX_UDF_TAG_FILE_ENTRY 261U
#define XX_UDF_TAG_EXTENDED_FILE_ENTRY 266U

/* ECMA-167 4/14.6.6 ICB file types. */
#define XX_UDF_ICB_FILE_TYPE_DIRECTORY 4U
#define XX_UDF_ICB_FILE_TYPE_FILE 5U

/* ECMA-167 4/14.4.3 file characteristics. */
#define XX_UDF_FID_DIRECTORY 0x02U
#define XX_UDF_FID_DELETED 0x04U
#define XX_UDF_FID_PARENT 0x08U

typedef struct xx_udf_tag_s {
    uint16_t identifier;
    uint16_t version;
    uint16_t serial;
    uint16_t crc;
    uint16_t crc_length;
    uint32_t location;
} xx_udf_tag;

typedef struct xx_udf_partition_s {
    uint16_t number;
    uint32_t start;  /* First logical sector of the partition. */
    uint32_t length; /* Partition length in logical blocks. */
} xx_udf_partition;

typedef struct xx_udf_entry_s {
    char *name;
    int64_t header_offset; /* File Entry offset. */
    int64_t data_offset;
    int64_t data_size;
    bool is_folder;
} xx_udf_entry;

typedef struct xx_udf_private_s {
    xx_udf_entry *entries;
    size_t count;
    size_t capacity;
    size_t member_count;
    int64_t *visited;
    size_t visited_count;
    size_t visited_capacity;
    xx_udf_partition partitions[XX_UDF_MAX_PARTITIONS];
    size_t partition_count;
    uint16_t map_numbers[XX_UDF_MAX_PARTITION_MAPS];
    bool map_valid[XX_UDF_MAX_PARTITION_MAPS];
    size_t map_count;
    int64_t anchor_offset;
    int64_t volume_end;
    uint32_t block_size;
    uint16_t udf_revision;
    char volume_identifier[256];
    char volume_set_identifier[512];
} xx_udf_private;

typedef struct xx_udf_archive_stream_s {
    xx_udf_private parsed;
    size_t index;
} xx_udf_archive_stream;

/* Result of resolving the allocation descriptors of a File Entry. */
typedef enum xx_udf_extent_result_e {
    XX_UDF_EXTENT_OK = 0,
    XX_UDF_EXTENT_UNSUPPORTED = 1, /* Well formed, but not representable here. */
    XX_UDF_EXTENT_ERROR = 2        /* Malformed / out of bounds. */
} xx_udf_extent_result;

static void xx_udf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

static uint16_t xx_udf_read16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_udf_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t xx_udf_read64(const uint8_t *data) {
    return (uint64_t)xx_udf_read32(data) |
           ((uint64_t)xx_udf_read32(data + 4U) << 32);
}

static bool xx_udf_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, XX_RT_SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* left + right with an overflow guard; both sides stay non negative. */
static bool xx_udf_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* ECMA-167 4/7.2.3: sum of bytes 0-3 and 5-15 of the tag, modulo 256. */
static uint8_t xx_udf_tag_checksum(const uint8_t *tag_bytes) {
    uint32_t sum = 0U;
    unsigned index;
    for (index = 0U; index < XX_UDF_TAG_SIZE; ++index) {
        if (index != 4U) sum += tag_bytes[index];
    }
    return (uint8_t)(sum & 0xFFU);
}

/* ECMA-167 Annex A: CRC-ITU-T, polynomial 0x1021, initial value 0, no
 * reflection and no final xor. */
static uint16_t xx_udf_descriptor_crc(const uint8_t *data, size_t size) {
    uint16_t crc = 0U;
    size_t index;
    unsigned bit;
    for (index = 0U; index < size; ++index) {
        crc = (uint16_t)(crc ^ (uint16_t)((uint16_t)data[index] << 8));
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* Read and verify a descriptor tag.  The identifier, the descriptor version,
 * the reserved byte and the tag checksum are always checked, and so is the
 * descriptor CRC when the tag declares a body that fits in one logical sector.
 * verify_location additionally requires the tag to name the sector it is
 * recorded in - only the Anchor Volume Descriptor Pointer can be checked that
 * way, because every other descriptor stores a partition relative block
 * number. */
static bool xx_udf_check_tag(Abstractformat *self, int64_t offset,
                             uint16_t expected_identifier,
                             bool verify_location, xx_udf_tag *tag) {
    uint8_t tag_bytes[XX_UDF_TAG_SIZE];
    uint8_t body[XX_UDF_SECTOR_SIZE];
    xx_udf_tag decoded;
    int64_t total_size;
    int64_t body_offset;
    if (tag) xx_rt_memset(tag, 0, sizeof(*tag));
    if (!self || !self->device || offset < 0) return false;
    total_size = xx_io_total_size(self->device);
    if (total_size < (int64_t)XX_UDF_TAG_SIZE ||
        offset > total_size - (int64_t)XX_UDF_TAG_SIZE ||
        !xx_udf_read_at(self->device, offset, tag_bytes, sizeof(tag_bytes))) {
        return false;
    }
    decoded.identifier = xx_udf_read16(tag_bytes);
    decoded.version = xx_udf_read16(tag_bytes + 2U);
    decoded.serial = xx_udf_read16(tag_bytes + 6U);
    decoded.crc = xx_udf_read16(tag_bytes + 8U);
    decoded.crc_length = xx_udf_read16(tag_bytes + 10U);
    decoded.location = xx_udf_read32(tag_bytes + 12U);
    if (decoded.identifier != expected_identifier) return false;
    /* ECMA-167 4/7.2.2: version 2 (2nd edition, UDF <= 2.01) or 3 (3rd
     * edition, UDF 2.50/2.60). */
    if (decoded.version != 2U && decoded.version != 3U) return false;
    /* ECMA-167 4/7.2.4: byte 5 is reserved and shall be #00. */
    if (tag_bytes[5] != 0U) return false;
    if (xx_udf_tag_checksum(tag_bytes) != tag_bytes[4]) return false;
    if (verify_location) {
        int64_t relative = offset - self->base_address;
        if (relative < 0 || (relative % (int64_t)XX_UDF_SECTOR_SIZE) != 0 ||
            (int64_t)decoded.location != relative / (int64_t)XX_UDF_SECTOR_SIZE) {
            return false;
        }
    }
    if (decoded.crc_length != 0U) {
        if ((size_t)decoded.crc_length >
                (size_t)XX_UDF_SECTOR_SIZE - XX_UDF_TAG_SIZE ||
            !xx_udf_add(offset, XX_UDF_TAG_SIZE, &body_offset) ||
            body_offset > total_size ||
            (int64_t)decoded.crc_length > total_size - body_offset ||
            !xx_udf_read_at(self->device, body_offset, body,
                            (size_t)decoded.crc_length) ||
            xx_udf_descriptor_crc(body, (size_t)decoded.crc_length) !=
                decoded.crc) {
            return false;
        }
    }
    if (tag) *tag = decoded;
    return true;
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static size_t xx_udf_utf8_encode(uint32_t code_point, char *out) {
    if (code_point < 0x80U) {
        out[0] = (char)code_point;
        return 1U;
    }
    if (code_point < 0x800U) {
        out[0] = (char)(0xC0U | (code_point >> 6));
        out[1] = (char)(0x80U | (code_point & 0x3FU));
        return 2U;
    }
    if (code_point < 0x10000U) {
        out[0] = (char)(0xE0U | (code_point >> 12));
        out[1] = (char)(0x80U | ((code_point >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (code_point & 0x3FU));
        return 3U;
    }
    out[0] = (char)(0xF0U | (code_point >> 18));
    out[1] = (char)(0x80U | ((code_point >> 12) & 0x3FU));
    out[2] = (char)(0x80U | ((code_point >> 6) & 0x3FU));
    out[3] = (char)(0x80U | (code_point & 0x3FU));
    return 4U;
}

/* Decode an OSTA CS0 string into UTF-8.  Byte 0 is the compression id: 8 means
 * one byte per character (the value is the Unicode code point), 16 means
 * big endian UTF-16.  When strict is set, characters that must never appear in
 * a path component are rejected instead of being passed through. */
static bool xx_udf_cs0_to_utf8(const uint8_t *data, size_t size, char *out,
                               size_t out_capacity, bool strict) {
    size_t written = 0U;
    size_t index;
    uint8_t compression;
    if (!data || !out || out_capacity == 0U) return false;
    out[0] = '\0';
    if (size == 0U) return true;
    compression = data[0];
    if (size == 1U) return compression == 8U || compression == 16U;
    if (compression != 8U && compression != 16U) return false;
    if (compression == 16U && ((size - 1U) % 2U) != 0U) return false;
    for (index = 1U; index < size;) {
        uint32_t code_point;
        char encoded[4];
        size_t encoded_size;
        if (compression == 8U) {
            code_point = data[index];
            index += 1U;
        } else {
            uint32_t unit = (uint32_t)(((uint32_t)data[index] << 8) |
                                       (uint32_t)data[index + 1U]);
            index += 2U;
            if (unit >= 0xD800U && unit <= 0xDBFFU) {
                uint32_t low;
                if (index + 1U >= size) return false;
                low = (uint32_t)(((uint32_t)data[index] << 8) |
                                 (uint32_t)data[index + 1U]);
                if (low < 0xDC00U || low > 0xDFFFU) return false;
                index += 2U;
                code_point = 0x10000U + ((unit - 0xD800U) << 10) +
                             (low - 0xDC00U);
            } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
                return false;
            } else {
                code_point = unit;
            }
        }
        if (code_point == 0U) break; /* Treat NUL as a terminator. */
        if (strict && (code_point < 32U || code_point == (uint32_t)'/' ||
                       code_point == (uint32_t)'\\')) {
            return false;
        }
        encoded_size = xx_udf_utf8_encode(code_point, encoded);
        if (encoded_size > out_capacity - 1U - written) return false;
        xx_rt_memcpy(out + written, encoded, encoded_size);
        written += encoded_size;
    }
    out[written] = '\0';
    return true;
}

/* ECMA-167 1/7.2.12 dstring: the last byte of the field holds the number of
 * bytes actually used, the compression id included. */
static void xx_udf_dstring_to_utf8(const uint8_t *field, size_t field_size,
                                   char *out, size_t out_capacity) {
    size_t used;
    if (!field || !out || out_capacity == 0U) return;
    out[0] = '\0';
    if (field_size < 2U) return;
    used = field[field_size - 1U];
    if (used == 0U || used > field_size - 1U) return;
    if (!xx_udf_cs0_to_utf8(field, used, out, out_capacity, false)) {
        out[0] = '\0';
    }
}

static char *xx_udf_strdup(const char *text) {
    size_t size;
    char *copy;
    if (!text) return NULL;
    size = xx_str_len(text);
    copy = (char *)xx_mem_alloc(size + 1U);
    if (!copy) return NULL;
    if (size != 0U) xx_rt_memcpy(copy, text, size);
    copy[size] = '\0';
    return copy;
}

/* Reject anything that would let an entry name escape the extraction root or
 * name a device on Windows. */
static bool xx_udf_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if (xx_str_len(name) >= XX_UDF_MAX_NAME_SIZE) return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static char *xx_udf_join_name(const char *prefix, const char *name) {
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    size_t name_size = name ? xx_str_len(name) : 0U;
    char *combined;
    if (!name || name_size == 0U || prefix_size >= XX_UDF_MAX_NAME_SIZE ||
        name_size > XX_UDF_MAX_NAME_SIZE - prefix_size -
                        (prefix_size != 0U ? 1U : 0U)) {
        return NULL;
    }
    combined = (char *)xx_mem_alloc(prefix_size + name_size +
                                    (prefix_size != 0U ? 2U : 1U));
    if (!combined) return NULL;
    if (prefix_size != 0U) {
        xx_rt_memcpy(combined, prefix, prefix_size);
        combined[prefix_size] = '/';
        xx_rt_memcpy(combined + prefix_size + 1U, name, name_size);
        combined[prefix_size + 1U + name_size] = '\0';
    } else {
        xx_rt_memcpy(combined, name, name_size);
        combined[name_size] = '\0';
    }
    return combined;
}

/* ------------------------------------------------------------------ */
/* Parsed state bookkeeping                                            */
/* ------------------------------------------------------------------ */

static void xx_udf_private_cleanup(xx_udf_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) xx_str_free(parsed->entries[index].name);
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->visited) xx_mem_free(parsed->visited);
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    parsed->anchor_offset = -1;
    parsed->volume_end = -1;
}

static void xx_udf_private_reset(xx_udf_private *parsed) {
    if (!parsed) return;
    xx_rt_memset(parsed, 0, sizeof(*parsed));
    parsed->anchor_offset = -1;
    parsed->volume_end = -1;
}

static bool xx_udf_append_entry(xx_udf_private *parsed, xx_udf_entry *entry) {
    xx_udf_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_UDF_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) {
            return false;
        }
        grown = (xx_udf_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    if (!entry->is_folder) ++parsed->member_count;
    parsed->entries[parsed->count++] = *entry;
    xx_rt_memset(entry, 0, sizeof(*entry));
    return true;
}

/* Returns true when this File Entry was already walked, which also makes the
 * "out of room" case terminate the walk instead of growing without bound. */
static bool xx_udf_seen_file_entry(xx_udf_private *parsed, int64_t offset) {
    int64_t *grown;
    size_t index;
    size_t capacity;
    if (!parsed) return true;
    for (index = 0U; index < parsed->visited_count; ++index) {
        if (parsed->visited[index] == offset) return true;
    }
    if (parsed->visited_count >= XX_UDF_MAX_VISITED) return true;
    if (parsed->visited_count == parsed->visited_capacity) {
        capacity = parsed->visited_capacity ? parsed->visited_capacity * 2U : 16U;
        if (capacity < parsed->visited_count ||
            capacity > SIZE_MAX / sizeof(*parsed->visited)) {
            return true;
        }
        grown = (int64_t *)xx_mem_realloc(parsed->visited,
                                          capacity * sizeof(*parsed->visited));
        if (!grown) return true;
        parsed->visited = grown;
        parsed->visited_capacity = capacity;
    }
    parsed->visited[parsed->visited_count++] = offset;
    return false;
}

/* ------------------------------------------------------------------ */
/* Partition mapping                                                   */
/* ------------------------------------------------------------------ */

static const xx_udf_partition *xx_udf_find_partition(
    const xx_udf_private *parsed, uint16_t partition_reference) {
    size_t index;
    if (!parsed || parsed->partition_count == 0U) return NULL;
    /* A single physical partition and no usable map table is the common case
     * for the images this reader has to open; accept it directly. */
    if (parsed->map_count == 0U) {
        return parsed->partition_count == 1U ? &parsed->partitions[0] : NULL;
    }
    if ((size_t)partition_reference >= parsed->map_count ||
        !parsed->map_valid[partition_reference]) {
        return NULL;
    }
    for (index = 0U; index < parsed->partition_count; ++index) {
        if (parsed->partitions[index].number ==
            parsed->map_numbers[partition_reference]) {
            return &parsed->partitions[index];
        }
    }
    return NULL;
}

/* Translate a (partition reference, logical block number) pair into a file
 * offset, requiring the block to lie inside the declared partition. */
static bool xx_udf_lba_to_offset(Abstractformat *self,
                                 const xx_udf_private *parsed,
                                 uint16_t partition_reference, uint32_t block,
                                 int64_t *offset) {
    const xx_udf_partition *partition;
    uint64_t absolute_block;
    if (!self || !parsed || !offset || parsed->block_size == 0U) return false;
    partition = xx_udf_find_partition(parsed, partition_reference);
    if (!partition || block >= partition->length) return false;
    absolute_block = (uint64_t)partition->start + (uint64_t)block;
    if (absolute_block > UINT64_MAX / parsed->block_size) return false;
    return xx_udf_add(self->base_address,
                      absolute_block * parsed->block_size, offset);
}

/* ------------------------------------------------------------------ */
/* Volume Recognition Sequence                                         */
/* ------------------------------------------------------------------ */

static bool xx_udf_has_recognition_sequence(xx_io_device *device,
                                            int64_t base_address) {
    int64_t total_size;
    unsigned index;
    bool extended_area = false;
    if (!device || base_address < 0) return false;
    total_size = xx_io_total_size(device);
    for (index = 0U; index < XX_UDF_VRS_MAX_DESCRIPTORS; ++index) {
        uint8_t identifier[5];
        int64_t offset;
        if (!xx_udf_add(base_address,
                        (uint64_t)XX_UDF_VRS_OFFSET +
                            (uint64_t)index * XX_UDF_SECTOR_SIZE + 1U,
                        &offset) ||
            offset > total_size ||
            (int64_t)sizeof(identifier) > total_size - offset ||
            !xx_udf_read_at(device, offset, identifier, sizeof(identifier))) {
            break;
        }
        if (xx_rt_memcmp(identifier, "BEA01", 5U) == 0) {
            extended_area = true;
        } else if (xx_rt_memcmp(identifier, "TEA01", 5U) == 0) {
            break;
        } else if (extended_area &&
                   (xx_rt_memcmp(identifier, "NSR02", 5U) == 0 ||
                    xx_rt_memcmp(identifier, "NSR03", 5U) == 0)) {
            return true;
        } else if (xx_rt_memcmp(identifier, "CD001", 5U) != 0 &&
                   xx_rt_memcmp(identifier, "CDW02", 5U) != 0 &&
                   xx_rt_memcmp(identifier, "BOOT2", 5U) != 0) {
            break;
        }
    }
    return false;
}

bool xx_udf_device_has_recognition_sequence(xx_io_device *dev,
                                            int64_t base_address) {
    return xx_udf_has_recognition_sequence(dev, base_address);
}

/* ------------------------------------------------------------------ */
/* Anchor Volume Descriptor Pointer                                    */
/* ------------------------------------------------------------------ */

/* ECMA-167 3/10.2: the AVDP body is two extent_ad structures.  A usable anchor
 * has to describe a non empty, sector aligned main Volume Descriptor Sequence
 * that lies inside the volume. */
static bool xx_udf_read_anchor(Abstractformat *self, int64_t offset,
                               bool verify_location, int64_t *vds_offset,
                               uint32_t *vds_length) {
    uint8_t extent[8];
    uint32_t length;
    uint32_t location;
    int64_t total_size;
    int64_t resolved;
    if (!self || !self->device || !vds_offset || !vds_length) return false;
    if (!xx_udf_check_tag(self, offset,
                          XX_UDF_TAG_ANCHOR_VOLUME_DESCRIPTOR_POINTER,
                          verify_location, NULL)) {
        return false;
    }
    if (!xx_udf_read_at(self->device, offset + (int64_t)XX_UDF_TAG_SIZE, extent,
                        sizeof(extent))) {
        return false;
    }
    length = xx_udf_read32(extent);
    location = xx_udf_read32(extent + 4U);
    total_size = xx_io_total_size(self->device);
    if (length == 0U || (length % XX_UDF_SECTOR_SIZE) != 0U ||
        location == 0U ||
        !xx_udf_add(self->base_address,
                    (uint64_t)location * XX_UDF_SECTOR_SIZE, &resolved) ||
        resolved >= total_size ||
        (int64_t)XX_UDF_SECTOR_SIZE > total_size - resolved) {
        return false;
    }
    *vds_offset = resolved;
    *vds_length = length;
    return true;
}

/* ECMA-167 3/8.4.2 records an anchor at logical sector 256, at the last sector
 * of the volume space and/or at last-256; UDF 2.60 2.2.3 also allows 512. */
static bool xx_udf_find_anchor(Abstractformat *self, int64_t *anchor_offset,
                               int64_t *vds_offset, uint32_t *vds_length) {
    int64_t candidates[4];
    unsigned candidate_count = 0U;
    unsigned index;
    unsigned pass;
    int64_t total_size;
    int64_t usable;
    int64_t last_sector;
    if (!self || !self->device || !anchor_offset || !vds_offset ||
        !vds_length || self->base_address < 0) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    usable = total_size - self->base_address;
    if (usable < (int64_t)XX_UDF_VRS_OFFSET + (int64_t)XX_UDF_SECTOR_SIZE) {
        return false;
    }
    last_sector = usable / (int64_t)XX_UDF_SECTOR_SIZE - 1;
    candidates[candidate_count++] = (int64_t)256 * XX_UDF_SECTOR_SIZE;
    if (last_sector >= 0) {
        candidates[candidate_count++] = last_sector * (int64_t)XX_UDF_SECTOR_SIZE;
    }
    if (last_sector >= 256) {
        candidates[candidate_count++] =
            (last_sector - 256) * (int64_t)XX_UDF_SECTOR_SIZE;
    }
    candidates[candidate_count++] = (int64_t)512 * XX_UDF_SECTOR_SIZE;
    /* Pass 0 demands a fully self consistent anchor.  Pass 1 accepts a merely
     * checksum valid one, and only when the Volume Recognition Sequence has
     * independently declared the image to be UDF - without that a binary whose
     * bytes happen to read as tag identifier 2 would be accepted. */
    for (pass = 0U; pass < 2U; ++pass) {
        if (pass == 1U &&
            !xx_udf_has_recognition_sequence(self->device, self->base_address)) {
            break;
        }
        for (index = 0U; index < candidate_count; ++index) {
            int64_t offset;
            if (!xx_udf_add(self->base_address, (uint64_t)candidates[index],
                            &offset) ||
                offset > total_size ||
                (int64_t)XX_UDF_SECTOR_SIZE > total_size - offset) {
                continue;
            }
            if (xx_udf_read_anchor(self, offset, pass == 0U, vds_offset,
                                   vds_length)) {
                *anchor_offset = offset;
                return true;
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Volume Descriptor Sequence                                          */
/* ------------------------------------------------------------------ */

/* Logical Volume Descriptor, ECMA-167 3/10.6:
 *     0 tag(16), 16 VolumeDescriptorSequenceNumber(4),
 *    20 DescriptorCharacterSet(64), 84 LogicalVolumeIdentifier(128 dstring),
 *   212 LogicalBlockSize(4), 216 DomainIdentifier(32 regid),
 *   248 LogicalVolumeContentsUse(16 - the File Set Descriptor long_ad),
 *   264 MapTableLength(4), 268 NumberOfPartitionMaps(4),
 *   272 ImplementationIdentifier(32), 304 ImplementationUse(128),
 *   432 IntegritySequenceExtent(8), 440 PartitionMaps.
 * A long_ad is ExtentLength(4) + LogicalBlockNumber(4) +
 * PartitionReferenceNumber(2) + ImplementationUse(6), so the block the File
 * Set Descriptor lives at is at 252 and not at 248. */
static bool xx_udf_parse_logical_volume_descriptor(xx_udf_private *parsed,
                                                   const uint8_t *block,
                                                   uint32_t *fsd_block,
                                                   uint16_t *fsd_partition) {
    uint32_t map_table_length;
    uint32_t map_count;
    uint32_t fsd_extent_length;
    size_t position;
    size_t index;
    if (!parsed || !block || !fsd_block || !fsd_partition) return false;
    parsed->block_size = xx_udf_read32(block + 212U);
    /* OSTA UDF 2.2.4.2 requires the logical block size to equal the logical
     * sector size, which for an image recorded in a file is 2048. */
    if (parsed->block_size != XX_UDF_SECTOR_SIZE) {
        parsed->block_size = 0U;
        return false;
    }
    /* Domain Identifier: flags(1) + identifier(23) + suffix(8); for
     * "*OSTA UDF Compliant" the first two suffix bytes are the BCD revision. */
    if (xx_rt_memcmp(block + 217U, "*OSTA UDF Compliant", 19U) == 0) {
        parsed->udf_revision = xx_udf_read16(block + 240U);
    }
    fsd_extent_length = xx_udf_read32(block + 248U);
    if (fsd_extent_length == 0U) return false;
    *fsd_block = xx_udf_read32(block + 252U);
    *fsd_partition = xx_udf_read16(block + 256U);
    map_table_length = xx_udf_read32(block + 264U);
    map_count = xx_udf_read32(block + 268U);
    if (map_table_length > XX_UDF_SECTOR_SIZE - 440U) return false;
    if (map_count > XX_UDF_MAX_PARTITION_MAPS) {
        map_count = XX_UDF_MAX_PARTITION_MAPS;
    }
    position = 440U;
    for (index = 0U; index < (size_t)map_count; ++index) {
        uint8_t type;
        uint8_t length;
        if (position + 2U > 440U + (size_t)map_table_length) break;
        type = block[position];
        length = block[position + 1U];
        if (length < 2U ||
            position + (size_t)length > 440U + (size_t)map_table_length) {
            break;
        }
        /* Type 1 is a physical partition map: type(1), length(1)=6,
         * VolumeSequenceNumber(2), PartitionNumber(2).  Type 2 maps (virtual,
         * sparable, metadata) need a remapping layer this reader does not
         * implement, so their reference stays unresolvable. */
        if (type == 1U && length >= 6U) {
            parsed->map_numbers[index] = xx_udf_read16(block + position + 4U);
            parsed->map_valid[index] = true;
        } else {
            parsed->map_valid[index] = false;
        }
        parsed->map_count = index + 1U;
        position += (size_t)length;
    }
    return true;
}

/* Partition Descriptor, ECMA-167 3/10.5: 20 PartitionFlags,
 * 22 PartitionNumber, 24 PartitionContents(32), 56 PartitionContentsUse(128),
 * 184 AccessType, 188 PartitionStartingLocation, 192 PartitionLength. */
static void xx_udf_parse_partition_descriptor(xx_udf_private *parsed,
                                              const uint8_t *block) {
    xx_udf_partition partition;
    size_t index;
    if (!parsed || !block || parsed->partition_count >= XX_UDF_MAX_PARTITIONS) {
        return;
    }
    partition.number = xx_udf_read16(block + 22U);
    partition.start = xx_udf_read32(block + 188U);
    partition.length = xx_udf_read32(block + 192U);
    if (partition.length == 0U) return;
    for (index = 0U; index < parsed->partition_count; ++index) {
        if (parsed->partitions[index].number == partition.number) return;
    }
    parsed->partitions[parsed->partition_count++] = partition;
}

/* Primary Volume Descriptor, ECMA-167 3/10.1: 24 VolumeIdentifier(32 dstring),
 * 72 VolumeSetIdentifier(128 dstring). */
static void xx_udf_parse_primary_volume_descriptor(xx_udf_private *parsed,
                                                   const uint8_t *block) {
    if (!parsed || !block) return;
    if (parsed->volume_identifier[0] == '\0') {
        xx_udf_dstring_to_utf8(block + 24U, 32U, parsed->volume_identifier,
                               sizeof(parsed->volume_identifier));
    }
    if (parsed->volume_set_identifier[0] == '\0') {
        xx_udf_dstring_to_utf8(block + 72U, 128U,
                               parsed->volume_set_identifier,
                               sizeof(parsed->volume_set_identifier));
    }
}

static bool xx_udf_scan_volume_descriptor_sequence(Abstractformat *self,
                                                   xx_udf_private *parsed,
                                                   int64_t vds_offset,
                                                   uint32_t vds_length,
                                                   uint32_t *fsd_block,
                                                   uint16_t *fsd_partition,
                                                   xx_pd_struct *pd) {
    uint8_t block[XX_UDF_SECTOR_SIZE];
    uint64_t block_count;
    uint64_t index;
    int64_t total_size;
    bool have_logical_volume = false;
    if (!self || !self->device || !parsed || !fsd_block || !fsd_partition) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    block_count = (uint64_t)vds_length / XX_UDF_SECTOR_SIZE;
    if (block_count > XX_UDF_MAX_VDS_BLOCKS) block_count = XX_UDF_MAX_VDS_BLOCKS;
    for (index = 0U; index < block_count; ++index) {
        int64_t offset;
        xx_udf_tag tag;
        uint16_t identifier;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_udf_add(vds_offset, index * XX_UDF_SECTOR_SIZE, &offset) ||
            offset > total_size ||
            (int64_t)XX_UDF_SECTOR_SIZE > total_size - offset ||
            !xx_udf_read_at(self->device, offset, block, sizeof(block))) {
            break;
        }
        identifier = xx_udf_read16(block);
        if (identifier == XX_UDF_TAG_TERMINATING_DESCRIPTOR) break;
        /* An unreadable or corrupt descriptor ends the sequence rather than
         * failing the whole volume: the descriptors this reader needs may well
         * have been seen already. */
        if (!xx_udf_check_tag(self, offset, identifier, false, &tag)) break;
        if (identifier == XX_UDF_TAG_PRIMARY_VOLUME_DESCRIPTOR) {
            xx_udf_parse_primary_volume_descriptor(parsed, block);
        } else if (identifier == XX_UDF_TAG_PARTITION_DESCRIPTOR) {
            xx_udf_parse_partition_descriptor(parsed, block);
        } else if (identifier == XX_UDF_TAG_LOGICAL_VOLUME_DESCRIPTOR &&
                   !have_logical_volume) {
            if (xx_udf_parse_logical_volume_descriptor(parsed, block, fsd_block,
                                                       fsd_partition)) {
                have_logical_volume = true;
            }
        }
    }
    return have_logical_volume && parsed->partition_count != 0U;
}

/* ------------------------------------------------------------------ */
/* File Entries                                                        */
/* ------------------------------------------------------------------ */

/* Collapse the allocation descriptors of a File Entry into the single
 * (offset, size) range the archive record model can express.
 *
 * allocation_type comes from the ICB flags: 0 short_ad, 1 long_ad, 2 ext_ad,
 * 3 the data is embedded in the File Entry itself.  ext_ad, sparse or
 * unrecorded extents and allocation extent continuations (extent type 3) are
 * reported as UNSUPPORTED, as are files whose recorded extents are not
 * physically contiguous - representing those would need a multi range record. */
static xx_udf_extent_result xx_udf_resolve_extents(
    Abstractformat *self, const xx_udf_private *parsed, int64_t ad_offset,
    uint32_t ad_length, uint8_t allocation_type, uint16_t home_partition,
    uint64_t information_length, int64_t *data_offset, int64_t *data_size) {
    uint8_t descriptor[16];
    size_t descriptor_size;
    uint64_t descriptor_count;
    uint64_t index;
    uint64_t total_length = 0U;
    int64_t total_size;
    int64_t first_offset = -1;
    int64_t expected_offset = 0;
    if (!self || !self->device || !parsed || !data_offset || !data_size ||
        parsed->block_size == 0U) {
        return XX_UDF_EXTENT_ERROR;
    }
    total_size = xx_io_total_size(self->device);
    if (allocation_type == 3U) {
        /* Embedded data: the "allocation descriptor" area is the file body. */
        if (ad_length == 0U || information_length > (uint64_t)ad_length ||
            ad_offset > total_size ||
            (int64_t)ad_length > total_size - ad_offset) {
            return XX_UDF_EXTENT_ERROR;
        }
        *data_offset = ad_offset;
        *data_size = (int64_t)information_length;
        return XX_UDF_EXTENT_OK;
    }
    if (allocation_type == 0U) {
        descriptor_size = 8U;
    } else if (allocation_type == 1U) {
        descriptor_size = 16U;
    } else {
        return XX_UDF_EXTENT_UNSUPPORTED;
    }
    if (information_length == 0U) {
        *data_offset = ad_offset;
        *data_size = 0;
        return XX_UDF_EXTENT_OK;
    }
    descriptor_count = (uint64_t)ad_length / descriptor_size;
    if (descriptor_count == 0U) return XX_UDF_EXTENT_ERROR;
    if (descriptor_count > XX_UDF_MAX_EXTENTS) return XX_UDF_EXTENT_UNSUPPORTED;
    for (index = 0U; index < descriptor_count; ++index) {
        int64_t descriptor_offset;
        int64_t extent_offset;
        uint32_t raw_length;
        uint32_t extent_length;
        uint32_t extent_type;
        uint32_t block;
        uint16_t partition;
        uint64_t rounded;
        if (!xx_udf_add(ad_offset, index * descriptor_size,
                        &descriptor_offset) ||
            descriptor_offset > total_size ||
            (int64_t)descriptor_size > total_size - descriptor_offset ||
            !xx_udf_read_at(self->device, descriptor_offset, descriptor,
                            descriptor_size)) {
            return XX_UDF_EXTENT_ERROR;
        }
        raw_length = xx_udf_read32(descriptor);
        extent_type = raw_length >> 30;
        extent_length = raw_length & 0x3FFFFFFFU;
        if (extent_length == 0U) break;
        /* Type 1/2 are allocated-but-not-recorded and unallocated extents,
         * type 3 points at a continuation Allocation Extent Descriptor. */
        if (extent_type != 0U) return XX_UDF_EXTENT_UNSUPPORTED;
        block = xx_udf_read32(descriptor + 4U);
        partition = (descriptor_size == 16U) ? xx_udf_read16(descriptor + 8U)
                                             : home_partition;
        if (!xx_udf_lba_to_offset(self, parsed, partition, block,
                                  &extent_offset) ||
            extent_offset > total_size ||
            (int64_t)extent_length > total_size - extent_offset) {
            return XX_UDF_EXTENT_ERROR;
        }
        if (first_offset < 0) {
            first_offset = extent_offset;
        } else if (extent_offset != expected_offset) {
            return XX_UDF_EXTENT_UNSUPPORTED;
        }
        /* Every extent but the last has to be a whole number of blocks, so the
         * next one starts at the rounded up end of this one. */
        rounded = ((uint64_t)extent_length + parsed->block_size - 1U) /
                  parsed->block_size * parsed->block_size;
        if (!xx_udf_add(extent_offset, rounded, &expected_offset)) {
            return XX_UDF_EXTENT_ERROR;
        }
        if (extent_length > UINT64_MAX - total_length) {
            return XX_UDF_EXTENT_ERROR;
        }
        total_length += extent_length;
    }
    if (first_offset < 0 || total_length < information_length ||
        information_length > (uint64_t)INT64_MAX ||
        first_offset > total_size ||
        (int64_t)information_length > total_size - first_offset) {
        return XX_UDF_EXTENT_ERROR;
    }
    *data_offset = first_offset;
    *data_size = (int64_t)information_length;
    return XX_UDF_EXTENT_OK;
}

typedef struct xx_udf_file_entry_s {
    uint8_t file_type;
    uint8_t allocation_type;
    uint64_t information_length;
    int64_t ad_offset;
    uint32_t ad_length;
} xx_udf_file_entry;

/* Read a File Entry (tag 261) or an Extended File Entry (tag 266).  The two
 * differ: the extended form inserts ObjectSize and CreationTime and carries a
 * StreamDirectoryICB, which pushes the allocation descriptors from 176 to 216
 * and the two length fields from 168/172 to 208/212. */
static bool xx_udf_read_file_entry(Abstractformat *self,
                                   const xx_udf_private *parsed,
                                   int64_t offset, xx_udf_file_entry *entry) {
    uint8_t header[XX_UDF_SECTOR_SIZE];
    uint16_t identifier;
    uint16_t icb_flags;
    size_t fixed_size;
    uint32_t extended_attributes_length;
    int64_t total_size;
    if (!self || !self->device || !parsed || !entry) return false;
    xx_rt_memset(entry, 0, sizeof(*entry));
    total_size = xx_io_total_size(self->device);
    if (offset < 0 || offset > total_size ||
        (int64_t)XX_UDF_SECTOR_SIZE > total_size - offset ||
        !xx_udf_read_at(self->device, offset, header, sizeof(header))) {
        return false;
    }
    identifier = xx_udf_read16(header);
    if (identifier != XX_UDF_TAG_FILE_ENTRY &&
        identifier != XX_UDF_TAG_EXTENDED_FILE_ENTRY) {
        return false;
    }
    if (!xx_udf_check_tag(self, offset, identifier, false, NULL)) return false;
    /* ICB tag at 16: 12 FileType, 18 Flags (both relative to the ICB tag). */
    entry->file_type = header[16U + 11U];
    icb_flags = xx_udf_read16(header + 16U + 18U);
    entry->allocation_type = (uint8_t)(icb_flags & 0x07U);
    entry->information_length = xx_udf_read64(header + 56U);
    if (identifier == XX_UDF_TAG_FILE_ENTRY) {
        fixed_size = 176U;
        extended_attributes_length = xx_udf_read32(header + 168U);
        entry->ad_length = xx_udf_read32(header + 172U);
    } else {
        fixed_size = 216U;
        extended_attributes_length = xx_udf_read32(header + 208U);
        entry->ad_length = xx_udf_read32(header + 212U);
    }
    /* ECMA-167 4/14.9: a File Entry is recorded in a single logical block, so
     * both variable areas have to fit in what is left of it. */
    if ((uint64_t)extended_attributes_length + (uint64_t)entry->ad_length >
        (uint64_t)parsed->block_size - fixed_size) {
        return false;
    }
    if (!xx_udf_add(offset, (uint64_t)fixed_size + extended_attributes_length,
                    &entry->ad_offset)) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Directory walk                                                      */
/* ------------------------------------------------------------------ */

static bool xx_udf_walk(Abstractformat *self, xx_udf_private *parsed,
                        int64_t file_entry_offset, uint16_t partition,
                        const char *path, unsigned depth, xx_pd_struct *pd);

/* Parse the File Identifier Descriptors of one directory extent and recurse
 * into the children. */
static bool xx_udf_walk_directory_data(Abstractformat *self,
                                       xx_udf_private *parsed,
                                       int64_t data_offset, int64_t data_size,
                                       const char *path, unsigned depth,
                                       xx_pd_struct *pd) {
    int64_t position = 0;
    int64_t total_size;
    if (!self || !self->device || !parsed) return false;
    total_size = xx_io_total_size(self->device);
    if (data_size <= 0) return true;
    if (data_size > (int64_t)XX_UDF_MAX_DIR_SIZE || data_offset < 0 ||
        data_offset > total_size || data_size > total_size - data_offset) {
        return false;
    }
    while (position + 38 <= data_size) {
        uint8_t header[38];
        int64_t descriptor_offset;
        int64_t child_offset;
        uint8_t characteristics;
        uint8_t identifier_length;
        uint16_t implementation_length;
        uint32_t icb_length;
        uint32_t child_block;
        uint16_t child_partition;
        int64_t descriptor_size;
        int64_t padded_size;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_udf_add(data_offset, (uint64_t)position, &descriptor_offset) ||
            !xx_udf_read_at(self->device, descriptor_offset, header,
                            sizeof(header))) {
            return false;
        }
        /* A directory extent is padded with zeroes once the descriptors end;
         * anything that is not a valid File Identifier Descriptor terminates
         * this extent rather than the whole walk. */
        if (xx_udf_read16(header) != XX_UDF_TAG_FILE_IDENTIFIER_DESCRIPTOR ||
            !xx_udf_check_tag(self, descriptor_offset,
                              XX_UDF_TAG_FILE_IDENTIFIER_DESCRIPTOR, false,
                              NULL)) {
            break;
        }
        characteristics = header[18];
        identifier_length = header[19];
        icb_length = xx_udf_read32(header + 20U);
        child_block = xx_udf_read32(header + 24U);
        child_partition = xx_udf_read16(header + 28U);
        implementation_length = xx_udf_read16(header + 36U);
        descriptor_size = 38 + (int64_t)implementation_length +
                          (int64_t)identifier_length;
        padded_size = (descriptor_size + 3) & ~(int64_t)3;
        if (padded_size <= 0 || padded_size > data_size - position) break;
        if ((characteristics & (XX_UDF_FID_PARENT | XX_UDF_FID_DELETED)) == 0U &&
            identifier_length != 0U && icb_length != 0U) {
            uint8_t raw_name[255];
            char decoded[XX_UDF_MAX_NAME_UTF8];
            char *full_name;
            int64_t name_offset;
            if (!xx_udf_add(descriptor_offset,
                            38U + (uint64_t)implementation_length,
                            &name_offset) ||
                name_offset > total_size ||
                (int64_t)identifier_length > total_size - name_offset ||
                !xx_udf_read_at(self->device, name_offset, raw_name,
                                identifier_length)) {
                return false;
            }
            if (xx_udf_cs0_to_utf8(raw_name, identifier_length, decoded,
                                   sizeof(decoded), true) &&
                xx_udf_safe_name(decoded) &&
                xx_udf_lba_to_offset(self, parsed, child_partition, child_block,
                                     &child_offset)) {
                full_name = xx_udf_join_name(path, decoded);
                if (!full_name) return false;
                if (!xx_udf_walk(self, parsed, child_offset, child_partition,
                                 full_name, depth + 1U, pd)) {
                    xx_str_free(full_name);
                    return false;
                }
                xx_str_free(full_name);
            }
            /* A name this reader cannot represent, or a child the partition
             * map cannot resolve, is skipped: the rest of the directory is
             * still perfectly readable. */
        }
        position += padded_size;
    }
    return true;
}

/* Walk one File Entry.  path is "" for the root, which is not itself listed. */
static bool xx_udf_walk(Abstractformat *self, xx_udf_private *parsed,
                        int64_t file_entry_offset, uint16_t partition,
                        const char *path, unsigned depth, xx_pd_struct *pd) {
    xx_udf_file_entry file_entry;
    xx_udf_entry entry;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    xx_udf_extent_result extent_result;
    if (!self || !parsed || !path) return false;
    if (depth > XX_UDF_MAX_DEPTH || (pd && xx_pd_is_stopped(pd))) return false;
    if (!xx_udf_read_file_entry(self, parsed, file_entry_offset, &file_entry)) {
        /* Not a File Entry at all - skip this name, keep the volume. */
        return true;
    }
    if (file_entry.file_type != XX_UDF_ICB_FILE_TYPE_DIRECTORY &&
        file_entry.file_type != XX_UDF_ICB_FILE_TYPE_FILE) {
        /* Symbolic links, devices, sockets, stream directories and indirect
         * ICBs are deliberately not represented. */
        return true;
    }
    extent_result = xx_udf_resolve_extents(
        self, parsed, file_entry.ad_offset, file_entry.ad_length,
        file_entry.allocation_type, partition, file_entry.information_length,
        &data_offset, &data_size);
    if (extent_result == XX_UDF_EXTENT_ERROR) return false;
    if (file_entry.file_type == XX_UDF_ICB_FILE_TYPE_DIRECTORY) {
        /* Cycle guard: a directory reached a second time (an ICB pointing at
         * an ancestor, say) is not descended into again. */
        if (xx_udf_seen_file_entry(parsed, file_entry_offset)) return true;
        if (path[0] != '\0') {
            xx_rt_memset(&entry, 0, sizeof(entry));
            entry.name = xx_udf_strdup(path);
            if (!entry.name) return false;
            entry.header_offset = file_entry_offset;
            entry.data_offset = data_offset;
            entry.data_size = 0;
            entry.is_folder = true;
            if (!xx_udf_append_entry(parsed, &entry)) {
                xx_str_free(entry.name);
                return false;
            }
        }
        if (extent_result != XX_UDF_EXTENT_OK) return true;
        return xx_udf_walk_directory_data(self, parsed, data_offset, data_size,
                                          path, depth, pd);
    }
    if (extent_result != XX_UDF_EXTENT_OK || path[0] == '\0') return true;
    xx_rt_memset(&entry, 0, sizeof(entry));
    entry.name = xx_udf_strdup(path);
    if (!entry.name) return false;
    entry.header_offset = file_entry_offset;
    entry.data_offset = data_offset;
    entry.data_size = data_size;
    entry.is_folder = false;
    if (!xx_udf_append_entry(parsed, &entry)) {
        xx_str_free(entry.name);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Top level parse                                                     */
/* ------------------------------------------------------------------ */

/* File Set Descriptor, ECMA-167 4/14.1: 16 RecordingDateAndTime(12),
 * 28 InterchangeLevel, 30 MaximumInterchangeLevel, 32 CharacterSetList,
 * 36 MaximumCharacterSetList, 40 FileSetNumber, 44 FileSetDescriptorNumber,
 * 48 LogicalVolumeIdentifierCharacterSet(64), 112 LogicalVolumeIdentifier(128),
 * 240 FileSetCharacterSet(64), 304 FileSetIdentifier(32),
 * 336 CopyrightFileIdentifier(32), 368 AbstractFileIdentifier(32),
 * 400 RootDirectoryICB(long_ad). */
static bool xx_udf_parse(Abstractformat *self, xx_udf_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t file_set[XX_UDF_SECTOR_SIZE];
    int64_t anchor_offset = -1;
    int64_t vds_offset = 0;
    uint32_t vds_length = 0U;
    uint32_t fsd_block = 0U;
    uint16_t fsd_partition = 0U;
    int64_t fsd_offset = 0;
    int64_t root_offset = 0;
    uint32_t root_block;
    uint16_t root_partition;
    uint32_t root_extent_length;
    int64_t total_size;
    size_t index;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns, and cleaning up an uninitialised one
     * would free indeterminate pointers. */
    xx_udf_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_udf_find_anchor(self, &anchor_offset, &vds_offset, &vds_length)) {
        goto fail;
    }
    parsed->anchor_offset = anchor_offset;
    if (!xx_udf_scan_volume_descriptor_sequence(self, parsed, vds_offset,
                                                vds_length, &fsd_block,
                                                &fsd_partition, pd)) {
        goto fail;
    }
    if (!xx_udf_lba_to_offset(self, parsed, fsd_partition, fsd_block,
                              &fsd_offset) ||
        fsd_offset > total_size ||
        (int64_t)XX_UDF_SECTOR_SIZE > total_size - fsd_offset ||
        !xx_udf_check_tag(self, fsd_offset, XX_UDF_TAG_FILE_SET_DESCRIPTOR,
                          false, NULL) ||
        !xx_udf_read_at(self->device, fsd_offset, file_set, sizeof(file_set))) {
        goto fail;
    }
    root_extent_length = xx_udf_read32(file_set + 400U);
    root_block = xx_udf_read32(file_set + 404U);
    root_partition = xx_udf_read16(file_set + 408U);
    if (root_extent_length == 0U ||
        !xx_udf_lba_to_offset(self, parsed, root_partition, root_block,
                              &root_offset)) {
        goto fail;
    }
    /* The volume ends after the last block any partition claims; the anchor
     * itself may sit beyond that, at the last sector of the medium. */
    parsed->volume_end = -1;
    for (index = 0U; index < parsed->partition_count; ++index) {
        uint64_t end_block = (uint64_t)parsed->partitions[index].start +
                             (uint64_t)parsed->partitions[index].length;
        int64_t end_offset;
        if (end_block > UINT64_MAX / parsed->block_size) continue;
        if (xx_udf_add(self->base_address, end_block * parsed->block_size,
                       &end_offset) &&
            end_offset > parsed->volume_end) {
            parsed->volume_end = end_offset;
        }
    }
    if (anchor_offset >= 0 &&
        anchor_offset + (int64_t)XX_UDF_SECTOR_SIZE > parsed->volume_end) {
        parsed->volume_end = anchor_offset + (int64_t)XX_UDF_SECTOR_SIZE;
    }
    /* ECMA-167 3/8.4.2 puts a copy of the anchor at the last sector of the
     * volume space, so one recorded there means the volume reaches the end of
     * the medium and nothing after the partitions is an overlay. */
    {
        int64_t usable = total_size - self->base_address;
        int64_t last_anchor;
        int64_t ignored_offset;
        uint32_t ignored_length;
        if (usable >= (int64_t)XX_UDF_SECTOR_SIZE) {
            last_anchor = self->base_address +
                          (usable / (int64_t)XX_UDF_SECTOR_SIZE - 1) *
                              (int64_t)XX_UDF_SECTOR_SIZE;
            if (last_anchor != anchor_offset &&
                xx_udf_read_anchor(self, last_anchor, true, &ignored_offset,
                                   &ignored_length) &&
                last_anchor + (int64_t)XX_UDF_SECTOR_SIZE > parsed->volume_end) {
                parsed->volume_end = last_anchor + (int64_t)XX_UDF_SECTOR_SIZE;
            }
        }
    }
    if (parsed->volume_end < 0 || parsed->volume_end > total_size) {
        parsed->volume_end = total_size;
    }
    if (!xx_udf_walk(self, parsed, root_offset, root_partition, "", 0U, pd)) {
        goto fail;
    }
    return true;
fail:
    xx_udf_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------ */
/* Archive record plumbing                                             */
/* ------------------------------------------------------------------ */

static bool xx_udf_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_udf_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_udf_populate_record(xx_archive_record *record,
                                   const xx_udf_entry *entry) {
    uint64_t size;
    if (!record || !entry || !entry->name) return false;
    size = entry->is_folder ? 0U : (uint64_t)entry->data_size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = -1;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->is_folder ? 0 : entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           entry->is_folder);
}

static void xx_udf_archive_stream_free(void *pointer) {
    xx_udf_archive_stream *stream = (xx_udf_archive_stream *)pointer;
    if (!stream) return;
    xx_udf_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void xx_udf_init(xx_udf *udf, xx_io_device *dev, int64_t base_address) {
    if (!udf) return;
    xx_rt_memset(udf, 0, sizeof(*udf));
    xx_format_init(&udf->format, dev, base_address);
    udf->format.endian = XX_ENDIAN_LITTLE;
    udf->format.file_type = XX_UDF_FILE_TYPE_ID;
    udf->format.format_type = XX_TYPE_ARCHIVE;
    udf->format.is_archive = true;
    xx_format_set_mime_type(&udf->format, "application/x-udf-image");
    xx_format_set_extension(&udf->format, "udf");
    udf->format.check_is_valid = xx_udf_check_is_valid;
    udf->format.handle_base_info = xx_udf_handle_base_info;
    udf->format.get_format_size = xx_udf_get_format_size;
    udf->format.get_number_of_archive_records =
        xx_udf_get_number_of_archive_records;
    udf->format.create_archive_records_reading =
        xx_udf_create_archive_records_reading;
    udf->format.get_current_archive_record = xx_udf_get_current_archive_record;
    udf->format.unpack_current_archive_record =
        xx_udf_unpack_current_archive_record;
    udf->format.archive_record_move_to_next = xx_udf_archive_record_move_to_next;
    udf->format.free_archive_records_reading =
        xx_udf_free_archive_records_reading;
    udf->format.destroy = xx_udf_vtable_destroy;
    udf->anchor_offset = -1;
    udf->volume_end = -1;
}

xx_udf *xx_udf_create(xx_io_device *dev, int64_t base_address) {
    xx_udf *udf = (xx_udf *)xx_mem_alloc(sizeof(*udf));
    if (udf) xx_udf_init(udf, dev, base_address);
    return udf;
}

void xx_udf_destroy(xx_udf *udf) {
    if (!udf) return;
    if (udf->internal) {
        xx_udf_private_cleanup((xx_udf_private *)udf->internal);
        xx_mem_free(udf->internal);
        udf->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&udf->format);
}

static void xx_udf_vtable_destroy(Abstractformat *self) {
    xx_udf_destroy((xx_udf *)self);
}

void xx_udf_free(xx_udf *udf) {
    if (!udf) return;
    xx_udf_destroy(udf);
    xx_mem_free(udf);
}

bool xx_udf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_udf_private parsed;
    bool result = xx_udf_parse(self, &parsed, pd);
    xx_udf_private_cleanup(&parsed);
    return result;
}

bool xx_udf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_udf_private *parsed;
    xx_udf *udf = (xx_udf *)self;
    int64_t total_size;
    if (!self || !udf) return false;
    parsed = (xx_udf_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_udf_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (udf->internal) {
        xx_udf_private_cleanup((xx_udf_private *)udf->internal);
        xx_mem_free(udf->internal);
    }
    udf->internal = parsed;
    udf->number_of_records = parsed->count;
    udf->number_of_members = parsed->member_count;
    udf->logical_block_size = parsed->block_size;
    udf->udf_revision = parsed->udf_revision;
    udf->anchor_offset = parsed->anchor_offset;
    udf->volume_end = parsed->volume_end;
    xx_rt_memcpy(udf->volume_identifier, parsed->volume_identifier,
                 sizeof(udf->volume_identifier));
    xx_rt_memcpy(udf->volume_set_identifier, parsed->volume_set_identifier,
                 sizeof(udf->volume_set_identifier));
    self->format_size = parsed->volume_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->volume_end) {
        self->overlay_offset = parsed->volume_end;
        self->overlay_size = total_size - parsed->volume_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_udf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_udf_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_udf *)self)->number_of_records;
}

xx_archive_record_state *xx_udf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_udf_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_udf_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_udf_copy_options(&state->options, options) ||
        !xx_udf_parse(self, &stream->parsed, pd)) {
        xx_udf_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_udf_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_udf_populate_record(&state->current_record,
                               &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_udf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_udf_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_udf_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_udf_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_udf_populate_record(&state->current_record,
                                &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_udf_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool folder;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!xx_udf_safe_name(name)) return false;
    option = xx_udf_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat(base, "/");
        if (!destination) goto cleanup;
        {
            char *joined = xx_str_concat(destination, name);
            xx_str_free(destination);
            destination = joined;
        }
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    folder = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER,
                                             false);
    if (folder) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_udf_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_udf_get_number_of_records(const xx_udf *udf) {
    return udf ? udf->number_of_records : 0U;
}
uint64_t xx_udf_get_number_of_members(const xx_udf *udf) {
    return udf ? udf->number_of_members : 0U;
}
uint32_t xx_udf_get_logical_block_size(const xx_udf *udf) {
    return udf ? udf->logical_block_size : 0U;
}
uint16_t xx_udf_get_udf_revision(const xx_udf *udf) {
    return udf ? udf->udf_revision : 0U;
}
int64_t xx_udf_get_anchor_offset(const xx_udf *udf) {
    return udf ? udf->anchor_offset : -1;
}
int64_t xx_udf_get_volume_end(const xx_udf *udf) {
    return udf ? udf->volume_end : -1;
}
const char *xx_udf_get_volume_identifier(const xx_udf *udf) {
    return udf ? udf->volume_identifier : NULL;
}
const char *xx_udf_get_volume_set_identifier(const xx_udf *udf) {
    return udf ? udf->volume_set_identifier : NULL;
}
