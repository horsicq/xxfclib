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
#include "xxfclib/data/xx_data.h"

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
#define BINDER_FATSECT UINT32_C(0xFFFFFFFD)
#define BINDER_MAXREGSECT UINT32_C(0xFFFFFFFA)

#define BINDER_TYPE_UNALLOCATED 0U
#define BINDER_TYPE_STORAGE 1U
#define BINDER_TYPE_STREAM 2U
#define BINDER_TYPE_ROOT 5U

/* {59850400-6664-101B-B21C-00AA004BA90B} in on-disk (mixed endian) order. */
static const uint8_t binder_root_clsid[16] = {0x00U, 0x04U, 0x85U, 0x59U, 0x64U, 0x66U, 0x1BU, 0x10U, 0xB2U, 0x1CU, 0x00U, 0xAAU, 0x00U, 0x4BU, 0xA9U, 0x0BU};

typedef struct binder_member_s {
    char *name;
    int64_t header_offset; /* directory entry position in the file */
    int64_t data_offset;   /* first sector/mini sector position, -1 if empty */
    uint64_t size;
    uint32_t start_sector;
    uint32_t directory_index;
    bool mini;
    bool virtual_document;
    uint8_t *virtual_bytes;
} binder_member;

typedef struct binder_stream_s {
    xx_io_device *device;
    /* Borrowed only during the active parse or extraction call. It is cleared
     * before returning an iterator so no caller-owned progress pointer lives
     * beyond its operation. */
    xx_pd_struct *progress;
    bool general_cfbf;
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
    uint8_t *directory;
    size_t entry_count;
    int64_t archive_size;
} binder_stream;

static bool binder_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void binder_stream_free(void *opaque)
{
    binder_stream *stream = (binder_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
        if (stream->items[index].virtual_bytes) xx_mem_free(stream->items[index].virtual_bytes);
    }
    if (stream->items) xx_mem_free(stream->items);
    if (stream->fat) xx_mem_free(stream->fat);
    if (stream->minifat) xx_mem_free(stream->minifat);
    if (stream->mini_stream) xx_mem_free(stream->mini_stream);
    if (stream->directory) xx_mem_free(stream->directory);
    xx_mem_free(stream);
}

/* File offset of a whole sector.  Every caller has already proved that the
 * index is below sector_count, so the result is inside the file. */
static int64_t binder_sector_offset(const binder_stream *stream, uint32_t sector)
{
    return stream->base_address + (int64_t)stream->sector_size + (int64_t)sector * (int64_t)stream->sector_size;
}

/* Walk a chain, refusing anything that leaves the table or runs longer than
 * max_steps.  Because max_steps never exceeds the number of table slots, a
 * chain that revisits a sector necessarily trips the limit and is rejected. */
static bool binder_chain_length(const uint32_t *table, size_t table_count, uint32_t start, size_t max_steps, size_t *out_length, xx_pd_struct *pd)
{
    size_t length = 0U;
    uint32_t sector = start;
    while (sector != BINDER_ENDOFCHAIN) {
        if ((pd && xx_pd_is_stopped(pd)) || sector > BINDER_MAXREGSECT || (size_t)sector >= table_count || (size_t)sector >= max_steps || length >= max_steps)
            return false;
        ++length;
        sector = table[sector];
    }
    *out_length = length;
    return true;
}

static bool binder_read_fat_chain(const binder_stream *stream, uint32_t start, uint64_t size, uint8_t *out)
{
    uint64_t done = 0U;
    uint32_t sector = start;
    size_t steps = 0U;
    while (done < size) {
        uint64_t chunk = size - done;
        if ((stream->progress && xx_pd_is_stopped(stream->progress)) || sector > BINDER_MAXREGSECT || (size_t)sector >= stream->fat_count ||
            sector >= stream->sector_count || steps >= stream->sector_count)
            return false;
        if (chunk > (uint64_t)stream->sector_size) chunk = (uint64_t)stream->sector_size;
        if (!binder_read_at(stream->device, binder_sector_offset(stream, sector), out + done, (size_t)chunk)) return false;
        done += chunk;
        ++steps;
        sector = stream->fat[sector];
    }
    return true;
}

static bool binder_read_mini_chain(const binder_stream *stream, uint32_t start, uint64_t size, uint8_t *out)
{
    uint64_t done = 0U;
    uint32_t sector = start;
    size_t steps = 0U;
    while (done < size) {
        uint64_t chunk = size - done;
        uint64_t offset;
        if ((stream->progress && xx_pd_is_stopped(stream->progress)) || sector > BINDER_MAXREGSECT || (size_t)sector >= stream->minifat_count ||
            steps >= stream->minifat_count)
            return false;
        offset = (uint64_t)sector * (uint64_t)stream->mini_sector_size;
        if (offset > (uint64_t)stream->mini_stream_size) return false;
        if (chunk > (uint64_t)stream->mini_sector_size) chunk = (uint64_t)stream->mini_sector_size;
        if (chunk > (uint64_t)stream->mini_stream_size - offset) return false;
        xx_mem_copy(out + done, stream->mini_stream + (size_t)offset, (size_t)chunk);
        done += chunk;
        ++steps;
        sector = stream->minifat[sector];
    }
    return true;
}

/* A directory entry name is UTF-16LE.  Only the filesystem-facing form is
 * normalized: OLE reserves the 0x01..0x1F prefixes ("\005SummaryInformation")
 * that no file system accepts. */
static size_t binder_name_to_utf8(const uint8_t *raw, size_t raw_size, char *out, size_t out_capacity)
{
    size_t input = 0U, output = 0U;
    while (input + 1U < raw_size) {
        uint32_t code = xx_data_get_u16(raw + input, 2, 0, false);
        input += 2U;
        if (code == 0U) break;
        if (code >= 0xD800U && code <= 0xDBFFU && input + 1U < raw_size) {
            uint32_t low = xx_data_get_u16(raw + input, 2, 0, false);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
                input += 2U;
            }
        }
        if (code < 0x20U || code == 0x7FU || code == '/' || code == '\\' || code == ':' || code == '*' || code == '?' || code == '"' || code == '<' || code == '>' ||
            code == '|' || (code >= 0xD800U && code <= 0xDFFFU))
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
    while (output != 0U && (out[output - 1U] == ' ' || out[output - 1U] == '.')) --output;
    if (output == 0U) {
        if (out_capacity < 1U) return 0U;
        out[output++] = '_';
    }
    return output;
}

/* Allocated directory names must include exactly one final UTF-16 NUL, with
 * no embedded NUL or unpaired surrogate. Control prefixes are legal OLE names
 * and are normalized separately for the output filesystem. */
static bool binder_valid_name(const uint8_t *entry)
{
    size_t at, size = xx_data_get_u16(entry + 0x40U, 2, 0, false);
    if (size < 2 || size > BINDER_DIR_NAME_SIZE || (size & 1) || xx_data_get_u16(entry + size - 2, 2, 0, false) != 0) return false;
    for (at = 0; at + 2 < size; at += 2) {
        uint16_t c = xx_data_get_u16(entry + at, 2, 0, false);
        if (c == 0 || (c >= 0xdc00U && c <= 0xdfffU)) return false;
        if (c >= 0xd800U && c <= 0xdbffU) {
            uint16_t low;
            at += 2;
            if (at + 2 >= size) return false;
            low = xx_data_get_u16(entry + at, 2, 0, false);
            if (low < 0xdc00U || low > 0xdfffU) return false;
        }
    }
    return true;
}

/* MSI's 6-bit packed alphabet is an on-disk naming convention, not compressed
 * stream data. Expand it and spell OLE control prefixes as [N], matching the
 * reference extractor's filesystem representation. Binder names retain their
 * existing spelling so its public member names remain compatible. */
static size_t cfbf_name_to_utf8(const uint8_t *raw, size_t size, char *out, size_t capacity)
{
    static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._";
    uint8_t expanded[256];
    size_t at, used = 0;
    for (at = 0; at + 2 < size; at += 2) {
        uint16_t c = xx_data_get_u16(raw + at, 2, 0, false);
        uint16_t chars[5];
        size_t count = 1, i;
        chars[0] = c;
        if (c >= 0x3800U && c < 0x4800U) {
            unsigned pair = c - 0x3800U;
            chars[0] = (uint8_t)alphabet[pair & 63U];
            chars[1] = (uint8_t)alphabet[pair >> 6];
            count = 2;
        } else if (c >= 0x4800U && c < 0x4840U) {
            chars[0] = (uint8_t)alphabet[c - 0x4800U];
        } else if (c == 0x4840U) {
            chars[0] = '!';
        } else if (c < 32U) {
            chars[0] = '[';
            if (c >= 10) {
                chars[1] = (uint16_t)('0' + c / 10);
                chars[2] = (uint16_t)('0' + c % 10);
                chars[3] = ']';
                count = 4;
            } else {
                chars[1] = (uint16_t)('0' + c);
                chars[2] = ']';
                count = 3;
            }
        }
        if (used + count * 2 + 2 > sizeof(expanded)) return 0;
        for (i = 0; i < count; ++i) {
            expanded[used++] = (uint8_t)chars[i];
            expanded[used++] = (uint8_t)(chars[i] >> 8);
        }
    }
    expanded[used++] = 0;
    expanded[used++] = 0;
    return binder_name_to_utf8(expanded, used, out, capacity);
}

static bool binder_safe_output_name(const char *name)
{
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' || name[1] == ':') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') || (length == 2U && segment[0] == '.' && segment[1] == '.')) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static bool binder_add_member(binder_stream *stream, const binder_member *member)
{
    binder_member *grown;
    if (!stream || !member || stream->count >= BINDER_MAX_MEMBERS || stream->count > SIZE_MAX / sizeof(*grown) - 1U) return false;
    grown = (binder_member *)xx_mem_realloc(stream->items, (stream->count + 1U) * sizeof(*grown));
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

static int64_t binder_dir_entry_offset(const binder_walk *walk, size_t index)
{
    size_t per_sector = (size_t)(walk->stream->sector_size / BINDER_DIR_ENTRY_SIZE);
    size_t which = index / per_sector;
    if (per_sector == 0U || which >= walk->dir_sector_count) return -1;
    return binder_sector_offset(walk->stream, walk->dir_sectors[which]) + (int64_t)((index % per_sector) * BINDER_DIR_ENTRY_SIZE);
}

/* Validate a stream's declared size against the chain that actually backs it
 * before anything is recorded for it. */
static bool binder_validate_stream(const binder_stream *stream, uint32_t start, uint64_t size, bool mini)
{
    const uint32_t *table = mini ? stream->minifat : stream->fat;
    size_t table_count = mini ? stream->minifat_count : stream->fat_count;
    uint32_t unit = mini ? stream->mini_sector_size : stream->sector_size;
    uint32_t sector = start;
    uint64_t remaining = size;
    size_t steps = 0;
    if (size == 0U) return true;
    while (remaining != 0) {
        uint64_t part = remaining < unit ? remaining : unit;
        if ((stream->progress && xx_pd_is_stopped(stream->progress)) || sector > BINDER_MAXREGSECT || (size_t)sector >= table_count || steps++ >= table_count)
            return false;
        if (mini) {
            uint64_t offset = (uint64_t)sector * unit;
            if (offset > stream->mini_stream_size || part > (uint64_t)stream->mini_stream_size - offset) return false;
        } else if (sector >= stream->sector_count) return false;
        remaining -= part;
        sector = table[sector];
    }
    return sector == BINDER_ENDOFCHAIN;
}

static bool binder_walk_tree(binder_walk *walk, uint32_t index, unsigned depth)
{
    const uint8_t *entry;
    uint32_t left, right, child, start;
    uint64_t size;
    uint16_t name_length;
    uint8_t type;
    char name[BINDER_DIR_NAME_SIZE * 2U + 4U];
    size_t name_size, saved_length;
    if (walk->stream->progress && xx_pd_is_stopped(walk->stream->progress)) return false;
    if (index == BINDER_ENDOFCHAIN || index == BINDER_FREESECT) return true;
    if (depth >= BINDER_MAX_DEPTH || (size_t)index >= walk->entry_count) return false;
    if (walk->visited[index]) return false; /* cycle in the red-black tree */
    walk->visited[index] = 1U;

    entry = walk->directory + (size_t)index * BINDER_DIR_ENTRY_SIZE;
    name_length = xx_data_get_u16(entry + 0x40U, 2, 0, false);
    type = entry[0x42U];
    left = xx_data_get_u32(entry + 0x44U, 4, 0, false);
    right = xx_data_get_u32(entry + 0x48U, 4, 0, false);
    child = xx_data_get_u32(entry + 0x4CU, 4, 0, false);
    start = xx_data_get_u32(entry + 0x74U, 4, 0, false);
    size = (uint64_t)xx_data_get_u32(entry + 0x78U, 4, 0, false) | ((uint64_t)xx_data_get_u32(entry + 0x7CU, 4, 0, false) << 32U);
    /* Version 3 writers leave the high word of the size uninitialized. */
    if (walk->stream->sector_size == 512U) size &= UINT32_C(0xFFFFFFFF);

    if (type == BINDER_TYPE_UNALLOCATED) return false;
    if (type != BINDER_TYPE_STORAGE && type != BINDER_TYPE_STREAM) return false;
    if (!binder_valid_name(entry) || entry[0x43U] > 1U || (type == BINDER_TYPE_STREAM && child != BINDER_FREESECT && child != BINDER_ENDOFCHAIN)) return false;

    if (!binder_walk_tree(walk, left, depth + 1U)) return false;

    name_size = walk->stream->general_cfbf ? cfbf_name_to_utf8(entry, (size_t)name_length, name, sizeof(name) - 1U)
                                           : binder_name_to_utf8(entry, (size_t)name_length, name, sizeof(name) - 1U);
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
        if (size > (uint64_t)INT64_MAX || !binder_validate_stream(walk->stream, start, size, mini)) return false;
        if (saved_length == 0U && name_size == 6U && xx_rt_memcmp(name, "Binder", 6U) == 0) walk->has_binder_stream = true;
        xx_mem_zero(&member, sizeof(member));
        member.size = size;
        member.start_sector = start;
        member.directory_index = index;
        member.mini = mini;
        member.header_offset = header_offset;
        member.data_offset = -1;
        if (size != 0U && !mini && start < walk->stream->sector_count) member.data_offset = binder_sector_offset(walk->stream, start);
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
static bool binder_load_fat(binder_stream *stream, const uint8_t *header, uint32_t fat_sector_count, uint32_t difat_start, uint32_t difat_sector_count)
{
    const uint32_t per_sector = stream->sector_size / 4U;
    uint32_t *difat = NULL;
    uint8_t *sector = NULL;
    size_t difat_count = 0U, capacity, index;
    bool result = false;

    if (fat_sector_count == 0U || fat_sector_count > stream->sector_count) return false;
    /* The FAT may describe at most every sector of the file (plus a slack of
     * one sector for writers that round up). */
    if ((uint64_t)fat_sector_count * (uint64_t)per_sector > (uint64_t)stream->sector_count + (uint64_t)per_sector) return false;
    if (difat_sector_count > stream->sector_count) return false;
    if (difat_sector_count != (fat_sector_count <= 109U ? 0U : (fat_sector_count - 109U + per_sector - 2U) / (per_sector - 1U))) return false;
    if (difat_sector_count == 0 && difat_start != BINDER_ENDOFCHAIN && difat_start != BINDER_FREESECT) return false;

    capacity = (size_t)fat_sector_count;
    difat = (uint32_t *)xx_mem_alloc(capacity * sizeof(*difat));
    sector = (uint8_t *)xx_mem_alloc(stream->sector_size);
    if (!difat || !sector) goto done;

    for (index = 0U; index < 109U && difat_count < capacity; ++index) {
        uint32_t value = xx_data_get_u32(header + 0x4CU + index * 4U, 4, 0, false);
        if (value == BINDER_FREESECT) break;
        if (value >= stream->sector_count) goto done;
        difat[difat_count++] = value;
    }
    if (difat_count < capacity) {
        uint32_t current = difat_start;
        uint32_t steps = 0U;
        while (difat_count < capacity && current != BINDER_ENDOFCHAIN && current != BINDER_FREESECT) {
            if ((stream->progress && xx_pd_is_stopped(stream->progress)) || current >= stream->sector_count || steps >= difat_sector_count ||
                !binder_read_at(stream->device, binder_sector_offset(stream, current), sector, stream->sector_size))
                goto done;
            ++steps;
            for (index = 0U; index + 1U < per_sector && difat_count < capacity; ++index) {
                uint32_t value = xx_data_get_u32(sector + index * 4U, 4, 0, false);
                if (value == BINDER_FREESECT) break;
                if (value >= stream->sector_count) goto done;
                difat[difat_count++] = value;
            }
            current = xx_data_get_u32(sector + (per_sector - 1U) * 4U, 4, 0, false);
        }
        if (steps != difat_sector_count || current != BINDER_ENDOFCHAIN) goto done;
    }
    if (difat_count != capacity) goto done;

    stream->fat_count = (size_t)fat_sector_count * (size_t)per_sector;
    stream->fat = (uint32_t *)xx_mem_alloc(stream->fat_count * sizeof(*stream->fat));
    if (!stream->fat) {
        stream->fat_count = 0U;
        goto done;
    }
    for (index = 0U; index < difat_count; ++index) {
        size_t slot;
        if ((stream->progress && xx_pd_is_stopped(stream->progress)) ||
            !binder_read_at(stream->device, binder_sector_offset(stream, difat[index]), sector, stream->sector_size))
            goto done;
        for (slot = 0U; slot < per_sector; ++slot) stream->fat[index * per_sector + slot] = xx_data_get_u32(sector + slot * 4U, 4, 0, false);
    }
    /* Marking and restoring these reserved cells checks both each FAT-sector
     * marker and duplicate DIFAT references without a quadratic scan. */
    for (index = 0U; index < difat_count; ++index) {
        if (difat[index] >= stream->fat_count || stream->fat[difat[index]] != BINDER_FATSECT) goto done;
        stream->fat[difat[index]] = BINDER_FREESECT;
    }
    for (index = 0U; index < difat_count; ++index) stream->fat[difat[index]] = BINDER_FATSECT;
    result = true;
done:
    if (difat) xx_mem_free(difat);
    if (sector) xx_mem_free(sector);
    return result;
}

static bool binder_load_minifat(binder_stream *stream, uint32_t start, uint32_t sector_count)
{
    const uint32_t per_sector = stream->sector_size / 4U;
    uint8_t *sector;
    uint32_t current = start;
    size_t steps = 0U, index;
    if (sector_count > stream->sector_count) return false;
    if (sector_count == 0U) return start == BINDER_ENDOFCHAIN || start == BINDER_FREESECT;
    if (start == BINDER_ENDOFCHAIN || start == BINDER_FREESECT) return false;
    stream->minifat_count = (size_t)sector_count * (size_t)per_sector;
    stream->minifat = (uint32_t *)xx_mem_alloc(stream->minifat_count * sizeof(*stream->minifat));
    sector = (uint8_t *)xx_mem_alloc(stream->sector_size);
    if (!stream->minifat || !sector) {
        stream->minifat_count = 0U;
        if (sector) xx_mem_free(sector);
        return false;
    }
    while (steps < sector_count) {
        if ((stream->progress && xx_pd_is_stopped(stream->progress)) || current > BINDER_MAXREGSECT || (size_t)current >= stream->fat_count ||
            current >= stream->sector_count || !binder_read_at(stream->device, binder_sector_offset(stream, current), sector, stream->sector_size)) {
            xx_mem_free(sector);
            return false;
        }
        for (index = 0U; index < per_sector; ++index) stream->minifat[steps * per_sector + index] = xx_data_get_u32(sector + index * 4U, 4, 0, false);
        ++steps;
        current = stream->fat[current];
        if (current == BINDER_ENDOFCHAIN) break;
    }
    xx_mem_free(sector);
    return steps == sector_count && current == BINDER_ENDOFCHAIN;
}

#include "xx_binder_docs.inc"

static bool binder_parse_impl(Abstractformat *format, binder_stream **result, xx_pd_struct *pd)
{
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
    bool binder_clsid, is_binder;
    size_t dir_chain = 0U, entry_count, index, per_dir_sector;

    xx_mem_zero(&walk, sizeof(walk));
    if (!format || !format->device || !result || format->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)BINDER_HEADER_SIZE || !binder_read_at(format->device, format->base_address, header, sizeof(header))) return false;
    if (header[0] != 0xD0U || header[1] != 0xCFU || header[2] != 0x11U || header[3] != 0xE0U || header[4] != 0xA1U || header[5] != 0xB1U || header[6] != 0x1AU ||
        header[7] != 0xE1U)
        return false;

    byte_order = xx_data_get_u16(header + 0x1CU, 2, 0, false);
    major = xx_data_get_u16(header + 0x1AU, 2, 0, false);
    sector_shift = xx_data_get_u16(header + 0x1EU, 2, 0, false);
    mini_shift = xx_data_get_u16(header + 0x20U, 2, 0, false);
    dir_count_field = xx_data_get_u32(header + 0x28U, 4, 0, false);
    fat_sector_count = xx_data_get_u32(header + 0x2CU, 4, 0, false);
    dir_start = xx_data_get_u32(header + 0x30U, 4, 0, false);
    cutoff = xx_data_get_u32(header + 0x38U, 4, 0, false);
    minifat_start = xx_data_get_u32(header + 0x3CU, 4, 0, false);
    minifat_count = xx_data_get_u32(header + 0x40U, 4, 0, false);
    difat_start = xx_data_get_u32(header + 0x44U, 4, 0, false);
    difat_count = xx_data_get_u32(header + 0x48U, 4, 0, false);

    if (byte_order != 0xFFFEU || mini_shift != 6U || cutoff != 4096U) return false;
    for (index = 0x22U; index < 0x28U; ++index)
        if (header[index]) return false;
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
    stream->progress = pd;
    stream->general_cfbf = format->file_type == XX_FILE_TYPE_CFBF;
    stream->base_address = format->base_address;
    stream->sector_size = UINT32_C(1) << sector_shift;
    stream->mini_sector_size = UINT32_C(1) << mini_shift;
    stream->mini_cutoff = cutoff;
    {
        int64_t available = size - (int64_t)stream->sector_size;
        int64_t sectors = available / (int64_t)stream->sector_size;
        if (available < 0 || sectors < 2 || sectors > (int64_t)BINDER_MAXREGSECT) goto fail;
        stream->sector_count = (uint32_t)sectors;
        stream->archive_size = (int64_t)stream->sector_size + sectors * (int64_t)stream->sector_size;
    }
    if (major == 4U) {
        uint8_t padding[4096U - BINDER_HEADER_SIZE];
        if (dir_count_field == 0 || dir_count_field > stream->sector_count ||
            !binder_read_at(format->device, format->base_address + BINDER_HEADER_SIZE, padding, sizeof(padding)))
            goto fail;
        for (index = 0; index < sizeof(padding); ++index)
            if (padding[index]) goto fail;
    }

    if (!binder_load_fat(stream, header, fat_sector_count, difat_start, difat_count)) goto fail;
    if (!binder_load_minifat(stream, minifat_start, minifat_count)) goto fail;

    /* Directory chain. */
    if (!binder_chain_length(stream->fat, stream->fat_count, dir_start, stream->sector_count, &dir_chain, pd) || dir_chain == 0U) goto fail;
    if (major == 4U && dir_chain != dir_count_field) goto fail;
    per_dir_sector = (size_t)(stream->sector_size / BINDER_DIR_ENTRY_SIZE);
    if (dir_chain > (size_t)BINDER_MAX_DIR_ENTRIES / per_dir_sector) goto fail;
    entry_count = dir_chain * per_dir_sector;
    dir_sectors = (uint32_t *)xx_mem_alloc(dir_chain * sizeof(*dir_sectors));
    directory = (uint8_t *)xx_mem_alloc(dir_chain * stream->sector_size);
    if (!dir_sectors || !directory) goto fail;
    {
        uint32_t current = dir_start;
        for (index = 0U; index < dir_chain; ++index) {
            if ((pd && xx_pd_is_stopped(pd)) || current > BINDER_MAXREGSECT || current >= stream->sector_count ||
                !binder_read_at(stream->device, binder_sector_offset(stream, current), directory + index * stream->sector_size, stream->sector_size))
                goto fail;
            dir_sectors[index] = current;
            current = stream->fat[current];
        }
    }

    /* Root entry owns the mini stream. The class id is a Binder discriminator,
     * not a requirement for the general compound-file face. */
    if (directory[0x42U] != BINDER_TYPE_ROOT || !binder_valid_name(directory) || directory[0x43U] > 1U) goto fail;
    binder_clsid = xx_rt_memcmp(directory + 0x50U, binder_root_clsid, sizeof(binder_root_clsid)) == 0;
    root_start = xx_data_get_u32(directory + 0x74U, 4, 0, false);
    root_size = (uint64_t)xx_data_get_u32(directory + 0x78U, 4, 0, false) | ((uint64_t)xx_data_get_u32(directory + 0x7CU, 4, 0, false) << 32U);
    if (major == 3U) root_size &= UINT32_C(0xFFFFFFFF);
    if (root_size > (uint64_t)stream->sector_count * (uint64_t)stream->sector_size) goto fail;
    if (root_size != 0U) {
        size_t chain = 0U;
        if (!binder_chain_length(stream->fat, stream->fat_count, root_start, stream->sector_count, &chain, pd) ||
            (uint64_t)chain * (uint64_t)stream->sector_size < root_size || (uint64_t)chain * (uint64_t)stream->sector_size - root_size >= stream->sector_size ||
            root_size > (uint64_t)SIZE_MAX)
            goto fail;
        stream->mini_stream = (uint8_t *)xx_mem_alloc((size_t)root_size);
        if (!stream->mini_stream || !binder_read_fat_chain(stream, root_start, root_size, stream->mini_stream)) goto fail;
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
    if (!binder_walk_tree(&walk, xx_data_get_u32(directory + 0x4CU, 4, 0, false), 0U)) goto fail;
    /* Discriminator: a Binder always keeps its layout in a root-level
     * "Binder" stream.  Without it this is some other compound document. */
    is_binder = binder_clsid && walk.has_binder_stream;
    if (stream->general_cfbf ? is_binder : (!is_binder || stream->count == 0U)) goto fail;
    if (stream->general_cfbf) {
        /* Refuse allocated entries disconnected from the root directory tree. */
        for (index = 1; index < entry_count; ++index) {
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            if (directory[index * BINDER_DIR_ENTRY_SIZE + 0x42U] != BINDER_TYPE_UNALLOCATED && !visited[index]) goto fail;
        }
    }

    stream->directory = directory;
    stream->entry_count = entry_count;
    directory = NULL;
    xx_mem_free(dir_sectors);
    dir_sectors = NULL;
    if (!stream->general_cfbf && !binder_add_virtual_documents(stream)) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    xx_mem_free(path);
    xx_mem_free(visited);
    stream->progress = NULL;
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

static bool binder_parse(Abstractformat *format, binder_stream **result, xx_pd_struct *pd)
{
    int64_t cursor;
    bool valid;
    if (!format || !format->device || !result) return false;
    *result = NULL;
    cursor = xx_io_tell(format->device);
    if (cursor < 0) return false;
    valid = binder_parse_impl(format, result, pd);
    if (xx_io_seek64(format->device, cursor, SEEK_SET) != 0) {
        if (valid) {
            binder_stream_free(*result);
            *result = NULL;
        }
        return false;
    }
    return valid;
}

static bool binder_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool binder_set_record(xx_archive_record *record, const binder_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)BINDER_DIR_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool binder_extract(const binder_stream *stream, const binder_member *member, uint8_t **plain, size_t *plain_size)
{
    uint8_t *output;
    if (!stream || !member || !plain || !plain_size || member->size > (uint64_t)SIZE_MAX) return false;
    if (member->virtual_document) {
        output = (uint8_t *)xx_mem_alloc((size_t)member->size);
        if (!output) return false;
        xx_mem_copy(output, member->virtual_bytes, (size_t)member->size);
        *plain = output;
        *plain_size = (size_t)member->size;
        return true;
    }
    if (member->size == 0U) {
        *plain = NULL;
        *plain_size = 0U;
        return true;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    if (member->mini ? !binder_read_mini_chain(stream, member->start_sector, member->size, output)
                     : !binder_read_fat_chain(stream, member->start_sector, member->size, output)) {
        xx_mem_free(output);
        return false;
    }
    *plain = output;
    *plain_size = (size_t)member->size;
    return true;
}

/* Commit a completed stream atomically so cancellation never destroys an
 * existing file, including when the caller explicitly enables overwrite. */
static xx_io_device *binder_stage(const char *destination, char **stage)
{
    char *parent = xx_str_dup(destination);
    size_t index, cut = 0U;
    unsigned attempt;
    *stage = NULL;
    if (!parent) return NULL;
    for (index = 0U; parent[index]; ++index)
        if (parent[index] == '/' || parent[index] == '\\') cut = index + 1U;
    parent[cut] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[40], *candidate;
        xx_io_device *device;
        (void)xx_rt_snprintf(suffix, sizeof(suffix), ".xx_binder.tmp.%u", attempt);
        candidate = xx_str_concat(parent, suffix);
        if (!candidate) break;
        /* The caller's stream may itself have a temporary-looking name. */
        if (xx_rt_strcmp(destination + cut, suffix) == 0) {
            xx_str_free(candidate);
            continue;
        }
        device = xx_io_file_open(candidate, "wbx");
        if (device) {
            *stage = candidate;
            xx_str_free(parent);
            return device;
        }
        xx_str_free(candidate);
    }
    xx_str_free(parent);
    return NULL;
}

void xx_binder_init(xx_binder *archive, xx_io_device *device, int64_t base_address)
{
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
    archive->format.get_number_of_archive_records = xx_binder_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_binder_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_binder_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_binder_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_binder_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_binder_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_binder *xx_binder_create(xx_io_device *device, int64_t base_address)
{
    xx_binder *archive = (xx_binder *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_binder_init(archive, device, base_address);
    return archive;
}

void xx_binder_destroy(xx_binder *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_binder_free(xx_binder *archive)
{
    if (!archive) return;
    xx_binder_destroy(archive);
    xx_mem_free(archive);
}

bool xx_binder_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    binder_stream *stream;
    if (!binder_parse(format, &stream, pd)) return false;
    binder_stream_free(stream);
    return true;
}

bool xx_binder_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    binder_stream *stream;
    if (!format || !binder_parse(format, &stream, pd)) return false;
    if (format->file_type == XX_FILE_TYPE_CFBF) {
        xx_cfbf *archive = (xx_cfbf *)format;
        archive->number_of_records = stream->count;
        archive->archive_end = format->base_address + stream->archive_size;
    } else {
        xx_binder *archive = (xx_binder *)format;
        archive->number_of_records = stream->count;
        archive->archive_end = format->base_address + stream->archive_size;
    }
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    binder_stream_free(stream);
    return true;
}

int64_t xx_binder_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_binder_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_binder_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_binder_handle_base_info(format, pd)) ? format->number_of_archive_records : 0U;
}

xx_archive_record_state *xx_binder_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    binder_stream *stream;
    xx_archive_record_state *state;
    if (!binder_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        binder_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = binder_stream_free;
    state->total_records = stream->count;
    if (!binder_copy_options(&state->options, options) || (stream->count && !binder_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_binder_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_binder_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    binder_stream *stream;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!format || !state || state->format != format || !(stream = (binder_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = binder_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_binder_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    binder_stream *stream;
    binder_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    char *stage = NULL;
    xx_io_device *destination = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool overwrite = false;
    int64_t cursor;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (binder_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    cursor = xx_io_tell(format->device);
    if (cursor < 0) return false;
    member = &stream->items[stream->index];
    path_option = xx_format_resolve_extra_parameter(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (path_option && member->size > xx_var_get_u64(path_option)) goto done;
    /* This reader reassembles each fragmented stream in memory. */
    path_option = xx_format_resolve_extra_parameter(format, &state->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (path_option && member->size > xx_var_get_u64(path_option)) goto done;
    stream->progress = pd;
    if (!binder_safe_output_name(member->name) || !binder_extract(stream, member, &plain, &plain_size)) goto done;
    path_option = xx_format_resolve_extra_parameter(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path_option = xx_format_resolve_extra_parameter(format, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = path_option && xx_var_get_bool(path_option);
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        if ((pd && xx_pd_is_stopped(pd)) || (!overwrite && xx_io_file_exists_a(path))) goto done;
        destination = binder_stage(path, &stage);
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            if (pd && xx_pd_is_stopped(pd)) {
                result = false;
                break;
            }
            ssize_t amount = xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
        destination = NULL;
    }
done:
    stream->progress = NULL;
    if (pd && xx_pd_is_stopped(pd)) result = false;
    if (xx_io_seek64(format->device, cursor, SEEK_SET) != 0) result = false;
    if (destination && xx_io_close(destination) != 0) result = false;
    if (result && stage) result = xx_io_file_replace_a(stage, path, overwrite);
    if (stage) {
        if (!result) (void)xx_io_file_remove_a(stage);
        xx_str_free(stage);
    }
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_binder_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
