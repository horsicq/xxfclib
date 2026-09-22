/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Microsoft Office Binder document (.OBD / .OBT /
 * .OBZ).  The container is an OLE2 / CFBF (Compound File Binary Format)
 * compound file, the same container Word, Excel and PowerPoint use, so the
 * eight-byte CFBF signature alone identifies nothing.  A Binder is told apart
 * by the root storage's class id, {59850400-6664-101B-B21C-00AA004BA90B},
 * together with the Binder-specific "Binder" stream directly under the root.
 * Both hold for every sample in the corpus and neither holds for a plain
 * Word/Excel/PowerPoint document.
 *
 * Layout follows MS-CFB and was cross-checked against XArchive's
 * archives/xcfbf.cpp, whose header field offsets (not the ones quoted in
 * several secondary sources) match the samples byte for byte.
 *
 * Members are the compound file's streams, named by their storage path
 * ("1/WordDocument").  Everything is stored: unpacking only reassembles the
 * FAT or mini FAT chain.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/binder/xx_binder.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing file-type shim: the enum entry is added by the coordinator. */
#ifdef BINDER
#define XX_BINDER_FILE_TYPE XX_FILE_TYPE_BINDER
#else
#define XX_BINDER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BINDER_HEADER_SIZE 512U
#define BINDER_DIR_ENTRY_SIZE 128U
#define BINDER_DIR_NAME_SIZE 64U
#define BINDER_MAX_MEMBERS 65536U
#define BINDER_MAX_DIR_ENTRIES 262144U
#define BINDER_MAX_DEPTH 48U
#define BINDER_MAX_PATH 4096U

#define BINDER_ENDOFCHAIN UINT32_C(0xFFFFFFFE)
#define BINDER_FREESECT UINT32_C(0xFFFFFFFF)
#define BINDER_MAXREGSECT UINT32_C(0xFFFFFFFA)

#define BINDER_TYPE_UNALLOCATED 0U
#define BINDER_TYPE_STORAGE 1U
#define BINDER_TYPE_STREAM 2U
#define BINDER_TYPE_ROOT 5U

/* {59850400-6664-101B-B21C-00AA004BA90B} in on-disk (mixed endian) order. */
static const uint8_t binder_root_clsid[16] = {
    0x00U, 0x04U, 0x85U, 0x59U, 0x64U, 0x66U, 0x1BU, 0x10U,
    0xB2U, 0x1CU, 0x00U, 0xAAU, 0x00U, 0x4BU, 0xA9U, 0x0BU};

typedef struct binder_member_s {
    char *name;
    int64_t header_offset; /* directory entry position in the file */
    int64_t data_offset;   /* first sector/mini sector position, -1 if empty */
    uint64_t size;
    uint32_t start_sector;
    bool mini;
} binder_member;

typedef struct binder_stream_s {
    xx_io_device *device;
    int64_t base_address;
    binder_member *items;
    size_t count;
    size_t index;
    uint32_t sector_size;
    uint32_t mini_sector_size;
    uint32_t mini_cutoff;
    uint32_t sector_count; /* whole sectors present in the file */
    uint32_t *fat;
    size_t fat_count;
    uint32_t *minifat;
    size_t minifat_count;
    uint8_t *mini_stream;
    size_t mini_stream_size;
    int64_t archive_size;
} binder_stream;

static uint16_t binder_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t binder_le32(const uint8_t *bytes) {
    return (uint32_t)binder_le16(bytes) |
           ((uint32_t)binder_le16(bytes + 2U) << 16U);
}

static bool binder_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static void binder_stream_free(void *opaque) {
    binder_stream *stream = (binder_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->fat) xx_mem_free(stream->fat);
    if (stream->minifat) xx_mem_free(stream->minifat);
    if (stream->mini_stream) xx_mem_free(stream->mini_stream);
    xx_mem_free(stream);
}

/* File offset of a whole sector.  Every caller has already proved that the
 * index is below sector_count, so the result is inside the file. */
static int64_t binder_sector_offset(const binder_stream *stream,
                                    uint32_t sector) {
    return stream->base_address + (int64_t)BINDER_HEADER_SIZE +
           (int64_t)sector * (int64_t)stream->sector_size;
}

/* Walk a chain, refusing anything that leaves the table or runs longer than
 * max_steps.  Because max_steps never exceeds the number of table slots, a
 * chain that revisits a sector necessarily trips the limit and is rejected. */
static bool binder_chain_length(const uint32_t *table, size_t table_count,
                                uint32_t start, size_t max_steps,
                                size_t *out_length) {
    size_t length = 0U;
    uint32_t sector = start;
    while (sector != BINDER_ENDOFCHAIN) {
        if (sector > BINDER_MAXREGSECT || (size_t)sector >= table_count ||
            length >= max_steps)
            return false;
        ++length;
        sector = table[sector];
    }
    *out_length = length;
    return true;
}

static bool binder_read_fat_chain(const binder_stream *stream, uint32_t start,
                                  uint64_t size, uint8_t *out) {
    uint64_t done = 0U;
    uint32_t sector = start;
    size_t steps = 0U;
    while (done < size) {
        uint64_t chunk = size - done;
        if (sector > BINDER_MAXREGSECT || (size_t)sector >= stream->fat_count ||
            sector >= stream->sector_count || steps >= stream->sector_count)
            return false;
        if (chunk > (uint64_t)stream->sector_size)
            chunk = (uint64_t)stream->sector_size;
        if (!binder_read_at(stream->device, binder_sector_offset(stream, sector),
                            out + done, (size_t)chunk))
            return false;
        done += chunk;
        ++steps;
        sector = stream->fat[sector];
    }
    return true;
}

static bool binder_read_mini_chain(const binder_stream *stream, uint32_t start,
                                   uint64_t size, uint8_t *out) {
    uint64_t done = 0U;
    uint32_t sector = start;
    size_t steps = 0U;
    while (done < size) {
        uint64_t chunk = size - done;
        uint64_t offset;
        if (sector > BINDER_MAXREGSECT ||
            (size_t)sector >= stream->minifat_count ||
            steps >= stream->minifat_count)
            return false;
        offset = (uint64_t)sector * (uint64_t)stream->mini_sector_size;
        if (offset > (uint64_t)stream->mini_stream_size) return false;
        if (chunk > (uint64_t)stream->mini_sector_size)
            chunk = (uint64_t)stream->mini_sector_size;
        if (chunk > (uint64_t)stream->mini_stream_size - offset) return false;
        xx_mem_copy(out + done, stream->mini_stream + (size_t)offset,
                    (size_t)chunk);
        done += chunk;
        ++steps;
        sector = stream->minifat[sector];
    }
    return true;
}

/* A directory entry name is UTF-16LE.  Only the filesystem-facing form is
 * normalized: OLE reserves the 0x01..0x1F prefixes ("\005SummaryInformation")
 * that no file system accepts. */
static size_t binder_name_to_utf8(const uint8_t *raw, size_t raw_size,
                                  char *out, size_t out_capacity) {
    size_t input = 0U, output = 0U;
    while (input + 1U < raw_size) {
        uint32_t code = binder_le16(raw + input);
        input += 2U;
        if (code == 0U) break;
        if (code >= 0xD800U && code <= 0xDBFFU && input + 1U < raw_size) {
            uint32_t low = binder_le16(raw + input);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
                input += 2U;
            }
        }
        if (code < 0x20U || code == 0x7FU || code == '/' || code == '\\' ||
            code == ':' || code == '*' || code == '?' || code == '"' ||
            code == '<' || code == '>' || code == '|' ||
            (code >= 0xD800U && code <= 0xDFFFU))
            code = (uint32_t)'_';
        if (code < 0x80U) {
            if (output + 1U > out_capacity) return 0U;
            out[output++] = (char)code;
        } else if (code < 0x800U) {
            if (output + 2U > out_capacity) return 0U;
            out[output++] = (char)(0xC0U | (code >> 6U));
            out[output++] = (char)(0x80U | (code & 0x3FU));
        } else if (code < 0x10000U) {
            if (output + 3U > out_capacity) return 0U;
            out[output++] = (char)(0xE0U | (code >> 12U));
            out[output++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[output++] = (char)(0x80U | (code & 0x3FU));
        } else {
            if (output + 4U > out_capacity) return 0U;
            out[output++] = (char)(0xF0U | (code >> 18U));
            out[output++] = (char)(0x80U | ((code >> 12U) & 0x3FU));
            out[output++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[output++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    /* Trailing dots and spaces are not representable on every host. */
    while (output != 0U && (out[output - 1U] == ' ' || out[output - 1U] == '.'))
        --output;
    if (output == 0U) {
        if (out_capacity < 1U) return 0U;
        out[output++] = '_';
    }
    return output;
}

static bool binder_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static bool binder_add_member(binder_stream *stream,
                              const binder_member *member) {
    binder_member *grown;
    if (!stream || !member || stream->count >= BINDER_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (binder_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

typedef struct binder_walk_s {
    binder_stream *stream;
    const uint8_t *directory;
    size_t entry_count;
    const uint32_t *dir_sectors;
    size_t dir_sector_count;
    uint8_t *visited;
    char *path;
    size_t path_length;
    bool has_binder_stream;
} binder_walk;

static int64_t binder_dir_entry_offset(const binder_walk *walk, size_t index) {
    size_t per_sector =
        (size_t)(walk->stream->sector_size / BINDER_DIR_ENTRY_SIZE);
    size_t which = index / per_sector;
    if (per_sector == 0U || which >= walk->dir_sector_count) return -1;
    return binder_sector_offset(walk->stream, walk->dir_sectors[which]) +
           (int64_t)((index % per_sector) * BINDER_DIR_ENTRY_SIZE);
}

/* Validate a stream's declared size against the chain that actually backs it
 * before anything is recorded for it. */
static bool binder_validate_stream(const binder_stream *stream, uint32_t start,
                                   uint64_t size, bool mini) {
    size_t length = 0U;
    uint64_t capacity;
    uint32_t unit;
    if (size == 0U) return true;
    if (mini) {
        if (!binder_chain_length(stream->minifat, stream->minifat_count, start,
                                 stream->minifat_count, &length))
            return false;
        unit = stream->mini_sector_size;
        if ((uint64_t)length * (uint64_t)unit >
            (uint64_t)stream->mini_stream_size)
            return false;
    } else {
        if (!binder_chain_length(stream->fat, stream->fat_count, start,
                                 stream->sector_count, &length))
            return false;
        unit = stream->sector_size;
    }
    capacity = (uint64_t)length * (uint64_t)unit;
    if (size > capacity) return false;
    /* The chain must not be wildly larger than the payload either. */
    return capacity - size < (uint64_t)unit;
}

static bool binder_walk_tree(binder_walk *walk, uint32_t index,
                             unsigned depth) {
    const uint8_t *entry;
    uint32_t left, right, child, start;
    uint64_t size;
    uint16_t name_length;
    uint8_t type;
    char name[BINDER_DIR_NAME_SIZE * 2U + 4U];
    size_t name_size, saved_length;
    if (index == BINDER_ENDOFCHAIN || index == BINDER_FREESECT) return true;
    if (depth >= BINDER_MAX_DEPTH || (size_t)index >= walk->entry_count)
        return false;
    if (walk->visited[index]) return false; /* cycle in the red-black tree */
    walk->visited[index] = 1U;

    entry = walk->directory + (size_t)index * BINDER_DIR_ENTRY_SIZE;
    name_length = binder_le16(entry + 0x40U);
    type = entry[0x42U];
    left = binder_le32(entry + 0x44U);
    right = binder_le32(entry + 0x48U);
    child = binder_le32(entry + 0x4CU);
    start = binder_le32(entry + 0x74U);
    size = (uint64_t)binder_le32(entry + 0x78U) |
           ((uint64_t)binder_le32(entry + 0x7CU) << 32U);
    /* Version 3 writers leave the high word of the size uninitialized. */
    if (walk->stream->sector_size == 512U) size &= UINT32_C(0xFFFFFFFF);

    if (type == BINDER_TYPE_UNALLOCATED) return false;
    if (type != BINDER_TYPE_STORAGE && type != BINDER_TYPE_STREAM) return false;
    if (name_length < 2U || name_length > BINDER_DIR_NAME_SIZE ||
        (name_length & 1U) != 0U)
        return false;

    if (!binder_walk_tree(walk, left, depth + 1U)) return false;

    name_size = binder_name_to_utf8(entry, (size_t)name_length, name,
                                    sizeof(name) - 1U);
    if (name_size == 0U) return false;
    name[name_size] = 0;

    saved_length = walk->path_length;
    if (saved_length + name_size + 2U > BINDER_MAX_PATH) return false;
    if (saved_length != 0U) walk->path[walk->path_length++] = '/';
    xx_mem_copy(walk->path + walk->path_length, name, name_size);
    walk->path_length += name_size;
    walk->path[walk->path_length] = 0;

    if (type == BINDER_TYPE_STREAM) {
        binder_member member;
        bool mini = size < (uint64_t)walk->stream->mini_cutoff;
        int64_t header_offset = binder_dir_entry_offset(walk, (size_t)index);
        if (header_offset < 0) return false;
        if (size > (uint64_t)INT64_MAX ||
            !binder_validate_stream(walk->stream, start, size, mini))
            return false;
        if (saved_length == 0U && name_size == 6U &&
            xx_rt_memcmp(name, "Binder", 6U) == 0)
            walk->has_binder_stream = true;
        xx_mem_zero(&member, sizeof(member));
        member.size = size;
        member.start_sector = start;
        member.mini = mini;
        member.header_offset = header_offset;
        member.data_offset = -1;
        if (size != 0U && !mini && start < walk->stream->sector_count)
            member.data_offset = binder_sector_offset(walk->stream, start);
        member.name = xx_str_create(walk->path);
        if (!member.name) return false;
        if (!binder_add_member(walk->stream, &member)) {
            xx_str_free(member.name);
            return false;
        }
    } else if (!binder_walk_tree(walk, child, depth + 1U)) {
        return false;
    }

    walk->path_length = saved_length;
    walk->path[walk->path_length] = 0;

    return binder_walk_tree(walk, right, depth + 1U);
}

/* Gather the DIFAT (header entries plus any chained DIFAT sectors) and read
 * the FAT it points at.  Every sector index is checked against the sectors
 * the file actually has before it is used. */
static bool binder_load_fat(binder_stream *stream, const uint8_t *header,
                            uint32_t fat_sector_count, uint32_t difat_start,
                            uint32_t difat_sector_count) {
    const uint32_t per_sector = stream->sector_size / 4U;
    uint32_t *difat = NULL;
    uint8_t *sector = NULL;
    size_t difat_count = 0U, capacity, index;
    bool result = false;

    if (fat_sector_count == 0U || fat_sector_count > stream->sector_count)
        return false;
    /* The FAT may describe at most every sector of the file (plus a slack of
     * one sector for writers that round up). */
    if ((uint64_t)fat_sector_count * (uint64_t)per_sector >
        (uint64_t)stream->sector_count + (uint64_t)per_sector)
        return false;
    if (difat_sector_count > stream->sector_count) return false;

    capacity = (size_t)fat_sector_count;
    difat = (uint32_t *)xx_mem_alloc(capacity * sizeof(*difat));
    sector = (uint8_t *)xx_mem_alloc(stream->sector_size);
    if (!difat || !sector) goto done;

    for (index = 0U; index < 109U && difat_count < capacity; ++index) {
        uint32_t value = binder_le32(header + 0x4CU + index * 4U);
        if (value == BINDER_FREESECT) break;
        if (value >= stream->sector_count) goto done;
        difat[difat_count++] = value;
    }
    if (difat_count < capacity) {
        uint32_t current = difat_start;
        uint32_t steps = 0U;
        while (difat_count < capacity && current != BINDER_ENDOFCHAIN &&
               current != BINDER_FREESECT) {
            if (current >= stream->sector_count ||
                steps >= difat_sector_count ||
                !binder_read_at(stream->device,
                                binder_sector_offset(stream, current), sector,
                                stream->sector_size))
                goto done;
            ++steps;
            for (index = 0U; index + 1U < per_sector && difat_count < capacity;
                 ++index) {
                uint32_t value = binder_le32(sector + index * 4U);
                if (value == BINDER_FREESECT) break;
                if (value >= stream->sector_count) goto done;
                difat[difat_count++] = value;
            }
            current = binder_le32(sector + (per_sector - 1U) * 4U);
        }
    }
    if (difat_count != capacity) goto done;

    stream->fat_count = (size_t)fat_sector_count * (size_t)per_sector;
    stream->fat = (uint32_t *)xx_mem_alloc(stream->fat_count *
                                           sizeof(*stream->fat));
    if (!stream->fat) {
        stream->fat_count = 0U;
        goto done;
    }
    for (index = 0U; index < difat_count; ++index) {
        size_t slot;
        if (!binder_read_at(stream->device,
                            binder_sector_offset(stream, difat[index]), sector,
                            stream->sector_size))
            goto done;
        for (slot = 0U; slot < per_sector; ++slot)
            stream->fat[index * per_sector + slot] =
                binder_le32(sector + slot * 4U);
    }
    result = true;
done:
    if (difat) xx_mem_free(difat);
    if (sector) xx_mem_free(sector);
    return result;
}

static bool binder_load_minifat(binder_stream *stream, uint32_t start,
                                uint32_t sector_count) {
    const uint32_t per_sector = stream->sector_size / 4U;
    uint8_t *sector;
    uint32_t current = start;
    size_t steps = 0U, index;
    if (sector_count > stream->sector_count) return false;
    if (sector_count == 0U || start == BINDER_ENDOFCHAIN) return true;
    stream->minifat_count = (size_t)sector_count * (size_t)per_sector;
    stream->minifat = (uint32_t *)xx_mem_alloc(stream->minifat_count *
                                               sizeof(*stream->minifat));
    sector = (uint8_t *)xx_mem_alloc(stream->sector_size);
    if (!stream->minifat || !sector) {
        stream->minifat_count = 0U;
        if (sector) xx_mem_free(sector);
        return false;
    }
    while (steps < sector_count) {
        if (current > BINDER_MAXREGSECT ||
            (size_t)current >= stream->fat_count ||
            current >= stream->sector_count ||
            !binder_read_at(stream->device,
                            binder_sector_offset(stream, current), sector,
                            stream->sector_size)) {
            xx_mem_free(sector);
            return false;
        }
        for (index = 0U; index < per_sector; ++index)
            stream->minifat[steps * per_sector + index] =
                binder_le32(sector + index * 4U);
        ++steps;
        current = stream->fat[current];
        if (current == BINDER_ENDOFCHAIN) break;
    }
    xx_mem_free(sector);
    return steps == sector_count;
}

static bool binder_parse(Abstractformat *format, binder_stream **result) {
    uint8_t header[BINDER_HEADER_SIZE];
    binder_stream *stream = NULL;
    binder_walk walk;
    uint32_t *dir_sectors = NULL;
    uint8_t *directory = NULL;
    char *path = NULL;
    uint8_t *visited = NULL;
    int64_t total, size;
    uint32_t major, sector_shift, mini_shift, byte_order;
    uint32_t dir_count_field, fat_sector_count, dir_start, cutoff;
    uint32_t minifat_start, minifat_count, difat_start, difat_count;
    uint32_t root_start;
    uint64_t root_size;
    size_t dir_chain = 0U, entry_count, index, per_dir_sector;

    xx_mem_zero(&walk, sizeof(walk));
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)BINDER_HEADER_SIZE ||
        !binder_read_at(format->device, format->base_address, header,
                        sizeof(header)))
        return false;
    if (header[0] != 0xD0U || header[1] != 0xCFU || header[2] != 0x11U ||
        header[3] != 0xE0U || header[4] != 0xA1U || header[5] != 0xB1U ||
        header[6] != 0x1AU || header[7] != 0xE1U)
        return false;

    byte_order = binder_le16(header + 0x1CU);
    major = binder_le16(header + 0x1AU);
    sector_shift = binder_le16(header + 0x1EU);
    mini_shift = binder_le16(header + 0x20U);
    dir_count_field = binder_le32(header + 0x28U);
    fat_sector_count = binder_le32(header + 0x2CU);
    dir_start = binder_le32(header + 0x30U);
    cutoff = binder_le32(header + 0x38U);
    minifat_start = binder_le32(header + 0x3CU);
    minifat_count = binder_le32(header + 0x40U);
    difat_start = binder_le32(header + 0x44U);
    difat_count = binder_le32(header + 0x48U);

    if (byte_order != 0xFFFEU || mini_shift != 6U || cutoff != 4096U)
        return false;
    if (major == 3U) {
        if (sector_shift != 9U || dir_count_field != 0U) return false;
    } else if (major == 4U) {
        if (sector_shift != 12U) return false;
    } else {
        return false;
    }

    stream = (binder_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->device = format->device;
    stream->base_address = format->base_address;
    stream->sector_size = UINT32_C(1) << sector_shift;
    stream->mini_sector_size = UINT32_C(1) << mini_shift;
    stream->mini_cutoff = cutoff;
    {
        int64_t available = size - (int64_t)BINDER_HEADER_SIZE;
        int64_t sectors = available / (int64_t)stream->sector_size;
        if (sectors <= 0 || sectors > (int64_t)BINDER_MAXREGSECT) goto fail;
        stream->sector_count = (uint32_t)sectors;
        stream->archive_size = (int64_t)BINDER_HEADER_SIZE +
                               sectors * (int64_t)stream->sector_size;
    }
    if (major == 4U && dir_count_field > stream->sector_count) goto fail;

    if (!binder_load_fat(stream, header, fat_sector_count, difat_start,
                         difat_count))
        goto fail;
    if (!binder_load_minifat(stream, minifat_start, minifat_count)) goto fail;

    /* Directory chain. */
    if (!binder_chain_length(stream->fat, stream->fat_count, dir_start,
                             stream->sector_count, &dir_chain) ||
        dir_chain == 0U)
        goto fail;
    per_dir_sector = (size_t)(stream->sector_size / BINDER_DIR_ENTRY_SIZE);
    if (dir_chain > (size_t)BINDER_MAX_DIR_ENTRIES / per_dir_sector) goto fail;
    entry_count = dir_chain * per_dir_sector;
    dir_sectors = (uint32_t *)xx_mem_alloc(dir_chain * sizeof(*dir_sectors));
    directory = (uint8_t *)xx_mem_alloc(dir_chain * stream->sector_size);
    if (!dir_sectors || !directory) goto fail;
    {
        uint32_t current = dir_start;
        for (index = 0U; index < dir_chain; ++index) {
            if (current > BINDER_MAXREGSECT ||
                current >= stream->sector_count ||
                !binder_read_at(stream->device,
                                binder_sector_offset(stream, current),
                                directory + index * stream->sector_size,
                                stream->sector_size))
                goto fail;
            dir_sectors[index] = current;
            current = stream->fat[current];
        }
    }

    /* Root entry: it carries the Binder class id and owns the mini stream. */
    if (directory[0x42U] != BINDER_TYPE_ROOT ||
        xx_rt_memcmp(directory + 0x50U, binder_root_clsid,
                     sizeof(binder_root_clsid)) != 0)
        goto fail;
    root_start = binder_le32(directory + 0x74U);
    root_size = (uint64_t)binder_le32(directory + 0x78U) |
                ((uint64_t)binder_le32(directory + 0x7CU) << 32U);
    if (major == 3U) root_size &= UINT32_C(0xFFFFFFFF);
    if (root_size > (uint64_t)stream->sector_count *
                        (uint64_t)stream->sector_size)
        goto fail;
    if (root_size != 0U) {
        size_t chain = 0U;
        if (!binder_chain_length(stream->fat, stream->fat_count, root_start,
                                 stream->sector_count, &chain) ||
            (uint64_t)chain * (uint64_t)stream->sector_size < root_size ||
            root_size > (uint64_t)SIZE_MAX)
            goto fail;
        stream->mini_stream = (uint8_t *)xx_mem_alloc((size_t)root_size);
        if (!stream->mini_stream ||
            !binder_read_fat_chain(stream, root_start, root_size,
                                   stream->mini_stream))
            goto fail;
        stream->mini_stream_size = (size_t)root_size;
    }

    path = (char *)xx_mem_alloc(BINDER_MAX_PATH + 2U);
    visited = (uint8_t *)xx_mem_calloc(entry_count, 1U);
    if (!path || !visited) goto fail;
    path[0] = 0;
    walk.stream = stream;
    walk.directory = directory;
    walk.entry_count = entry_count;
    walk.dir_sectors = dir_sectors;
    walk.dir_sector_count = dir_chain;
    walk.visited = visited;
    walk.path = path;
    walk.visited[0] = 1U; /* the root itself is never a child */
    if (!binder_walk_tree(&walk, binder_le32(directory + 0x4CU), 0U)) goto fail;
    /* Discriminator: a Binder always keeps its layout in a root-level
     * "Binder" stream.  Without it this is some other compound document. */
    if (!walk.has_binder_stream || stream->count == 0U) goto fail;

    xx_mem_free(dir_sectors);
    xx_mem_free(directory);
    xx_mem_free(path);
    xx_mem_free(visited);
    *result = stream;
    return true;
fail:
    if (dir_sectors) xx_mem_free(dir_sectors);
    if (directory) xx_mem_free(directory);
    if (path) xx_mem_free(path);
    if (visited) xx_mem_free(visited);
    binder_stream_free(stream);
    return false;
}

static bool binder_copy_options(xx_list_s *destination,
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

static const xx_var *binder_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool binder_set_record(xx_archive_record *record,
                              const binder_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)BINDER_DIR_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool binder_extract(const binder_stream *stream,
                           const binder_member *member, uint8_t **plain,
                           size_t *plain_size) {
    uint8_t *output;
    if (!stream || !member || !plain || !plain_size ||
        member->size > (uint64_t)SIZE_MAX)
        return false;
    if (member->size == 0U) {
        *plain = NULL;
        *plain_size = 0U;
        return true;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    if (member->mini
            ? !binder_read_mini_chain(stream, member->start_sector,
                                      member->size, output)
            : !binder_read_fat_chain(stream, member->start_sector, member->size,
                                     output)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->size;
    return true;
}

void xx_binder_init(xx_binder *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BINDER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-msbinder");
    xx_format_set_extension(&archive->format, "obd");
    archive->format.check_is_valid = xx_binder_check_is_valid;
    archive->format.handle_base_info = xx_binder_handle_base_info;
    archive->format.get_format_size = xx_binder_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_binder_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_binder_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_binder_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_binder_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_binder_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_binder_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_binder *xx_binder_create(xx_io_device *device, int64_t base_address) {
    xx_binder *archive = (xx_binder *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_binder_init(archive, device, base_address);
    return archive;
}

void xx_binder_destroy(xx_binder *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_binder_free(xx_binder *archive) {
    if (!archive) return;
    xx_binder_destroy(archive);
    xx_mem_free(archive);
}

bool xx_binder_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    binder_stream *stream;
    (void)pd;
    if (!binder_parse(format, &stream)) return false;
    binder_stream_free(stream);
    return true;
}

bool xx_binder_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    binder_stream *stream;
    xx_binder *archive;
    (void)pd;
    if (!format || !binder_parse(format, &stream)) return false;
    archive = (xx_binder *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    binder_stream_free(stream);
    return true;
}

int64_t xx_binder_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binder_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_binder_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_binder_handle_base_info(format, pd))
               ? ((xx_binder *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_binder_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    binder_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!binder_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        binder_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = binder_stream_free;
    state->total_records = stream->count;
    if (!binder_copy_options(&state->options, options) ||
        !binder_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_binder_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_binder_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    binder_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (binder_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = binder_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_binder_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    binder_stream *stream;
    binder_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (binder_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!binder_safe_output_name(member->name) ||
        !binder_extract(stream, member, &plain, &plain_size)) goto done;
    path_option = binder_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_binder_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
