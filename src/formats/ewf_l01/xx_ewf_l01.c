/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * EnCase logical evidence file (EWF-L01). The layout is summarised in
 * xx_ewf_l01.h. This file was written from the published libyal EWF format
 * notes; no implementation code was copied.
 *
 * One walk over the section chain collects the chunk tables (the media
 * stream the logical files are cut from) and loads the ltree text. The
 * ltree's "entry" category is then read as a pre-order tree with an
 * explicit stack, so neither a deep nor a wide tree can recurse. Members
 * are produced on demand: each one decodes only the chunks its extents
 * touch, through a one-chunk cache.
 *
 * Hostile input: every descriptor must carry a correct Adler-32 and point
 * strictly forward, chunk offsets inside one table must increase and stay
 * inside the section that holds them, every count is capped and every
 * allocation is proportional to bytes actually present in the file. A
 * damaged table whose mirror (table2) is damaged too stops the chunk list
 * there, so no later chunk is ever attributed to the wrong media offset.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ewf_l01/xx_ewf_l01.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared, so the alias macro that
 * sits next to the enumerator is tested instead. */
#ifdef EWF_L01
#define XX_EWF_L01_FILE_TYPE XX_FILE_TYPE_EWF_L01
#else
#define XX_EWF_L01_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define L01_FILE_HEADER_SIZE 13
#define L01_DESCRIPTOR_SIZE 76
#define L01_VOLUME_SIZE 1052U
#define L01_VOLUME_SMALL_SIZE 94U
#define L01_TABLE_HEADER_SIZE 24U
#define L01_LTREE_HEADER_SIZE 48U

/* Only bounds what a crafted chain can ask for; real files have a handful
 * of sections per segment plus two per 65534 chunks. */
#define L01_MAX_SECTIONS UINT32_C(0x100000)
/* 4 Mi chunks of 32 KiB each is 128 GiB of logical data. */
#define L01_MAX_CHUNKS UINT32_C(0x400000)
#define L01_MAX_CHUNK_SIZE (UINT32_C(16) << 20)
/* The ltree text is stored uncompressed, so this can only be reached by a
 * file at least this large. */
#define L01_MAX_LTREE_BYTES (UINT64_C(256) << 20)
#define L01_MAX_ENTRIES UINT32_C(0x400000)
#define L01_MAX_DEPTH 256U
#define L01_MAX_TYPES 4096U
#define L01_MAX_PATH 32768U
#define L01_NO_PARENT UINT32_MAX
/* A sparse entry is one stored byte repeated "ls" times; the size is not
 * backed by stored data, so it gets an absolute ceiling. */
#define L01_MAX_SPARSE_SIZE (UINT64_C(4) << 30)
#define L01_FLAG_SPARSE UINT32_C(0x04000000)
#define L01_FILL_BUFFER 65536U

static const uint8_t l01_signature[8] = {0x4CU, 0x56U, 0x46U, 0x09U, 0x0DU, 0x0AU, 0xFFU, 0x00U};

typedef enum l01_kind_e {
    L01_SECTION_OTHER = 0,
    L01_SECTION_VOLUME,
    L01_SECTION_SECTORS,
    L01_SECTION_TABLE,
    L01_SECTION_TABLE2,
    L01_SECTION_LTREE,
    L01_SECTION_NEXT,
    L01_SECTION_DONE
} l01_kind;

typedef struct l01_chunk_s {
    int64_t at;      /**< Absolute device offset of the stored chunk. */
    uint32_t length; /**< Stored bytes, checksum or zlib trailer included. */
    uint8_t compressed;
} l01_chunk;

typedef struct l01_entry_s {
    uint32_t parent;  /**< Entry index, or L01_NO_PARENT below the root. */
    uint32_t name_at; /**< UTF-16 code units into the ltree text. */
    uint32_t name_len;
    uint32_t be_at;
    uint32_t be_len;
    uint32_t flags;
    uint64_t size;
    int64_t du; /**< Duplicate data offset, -1 when not set. */
    uint8_t dir;
    uint8_t bad; /**< A value could not be read: never extracted. */
} l01_entry;

typedef struct l01_file_s {
    uint32_t entry;
    uint8_t renamed;
    uint8_t blocked; /**< Still collides after renaming. */
} l01_file;

typedef struct l01_parsed_s {
    int64_t input_size;
    int64_t format_end;
    uint32_t chunk_size;
    uint16_t segment;
    bool has_ltree;
    bool finished;
    bool chunks_broken;
    l01_chunk *chunks;
    size_t chunk_count;
    size_t chunk_capacity;
    uint8_t *text; /**< UTF-16LE ltree text. */
    size_t text_units;
    l01_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    l01_file *files;
    size_t file_count;
} l01_parsed;

typedef struct l01_range_s {
    int64_t start;
    int64_t end;
} l01_range;

typedef struct l01_stream_s {
    l01_parsed parsed;
    size_t index;
    char *name;     /**< L01_MAX_PATH + suffix room. */
    uint8_t *chunk; /**< chunk_size + 4 bytes. */
    uint8_t *packed;
    size_t packed_capacity;
    xx_io_device *chunk_device;
    size_t cached; /**< Chunk held in `chunk`, or SIZE_MAX. */
    size_t cached_length;
} l01_stream;

/* ------------------------------------------------------------- helpers -- */

static bool l01_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool l01_write_all(xx_io_device *output, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    while (done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

static bool l01_within(int64_t total, int64_t offset, uint64_t size)
{
    return total >= 0 && offset >= 0 && offset <= total && size <= (uint64_t)(total - offset);
}

static void l01_parsed_free(l01_parsed *p)
{
    if (!p) return;
    if (p->chunks) xx_mem_free(p->chunks);
    if (p->text) xx_mem_free(p->text);
    if (p->entries) xx_mem_free(p->entries);
    if (p->files) xx_mem_free(p->files);
    xx_mem_zero(p, sizeof(*p));
}

static uint32_t l01_unit(const l01_parsed *p, size_t index)
{
    return (uint32_t)p->text[index * 2U] | ((uint32_t)p->text[index * 2U + 1U] << 8);
}

/* ------------------------------------------------------ file structure -- */

static bool l01_read_file_header(xx_io_device *device, int64_t input_size, int64_t base, uint16_t *segment)
{
    uint8_t header[L01_FILE_HEADER_SIZE];
    uint16_t number;
    if (!l01_within(input_size, base, L01_FILE_HEADER_SIZE) || !l01_read_at(device, base, header, sizeof(header)) ||
        xx_rt_memcmp(header, l01_signature, sizeof(l01_signature)) != 0 || header[8] != 0x01U || header[11] != 0U || header[12] != 0U)
        return false;
    number = (uint16_t)(header[9] | (header[10] << 8));
    if (number == 0U) return false;
    if (segment) *segment = number;
    return true;
}

static l01_kind l01_classify(const uint8_t *type)
{
    static const struct {
        const char *name;
        l01_kind kind;
    } names[] = {{"volume", L01_SECTION_VOLUME},   {"disk", L01_SECTION_VOLUME}, {"data", L01_SECTION_VOLUME},
                 {"sectors", L01_SECTION_SECTORS}, {"table", L01_SECTION_TABLE}, {"table2", L01_SECTION_TABLE2},
                 {"ltree", L01_SECTION_LTREE},     {"next", L01_SECTION_NEXT},   {"done", L01_SECTION_DONE}};
    size_t index;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        size_t length = xx_str_len(names[index].name);
        size_t rest;
        bool padded = true;
        if (xx_rt_memcmp(type, names[index].name, length) != 0) continue;
        for (rest = length; rest < 16U; ++rest)
            if (type[rest] != 0U) padded = false;
        if (padded) return names[index].kind;
    }
    return L01_SECTION_OTHER;
}

typedef struct l01_descriptor_s {
    l01_kind kind;
    uint64_t next;
    uint64_t size;
} l01_descriptor;

static bool l01_read_descriptor(xx_io_device *device, int64_t input_size, int64_t offset, l01_descriptor *out)
{
    uint8_t raw[L01_DESCRIPTOR_SIZE];
    if (!l01_within(input_size, offset, L01_DESCRIPTOR_SIZE) || !l01_read_at(device, offset, raw, sizeof(raw)) ||
        xx_adler32(raw, 72U) != xx_data_get_u32(raw, sizeof(raw), 72U, false))
        return false;
    out->kind = l01_classify(raw);
    out->next = xx_data_get_u64(raw, sizeof(raw), 16U, false);
    out->size = xx_data_get_u64(raw, sizeof(raw), 24U, false);
    return true;
}

static void l01_read_geometry(xx_io_device *device, l01_parsed *p, int64_t data_at, uint64_t data_size)
{
    uint8_t volume[L01_VOLUME_SIZE];
    size_t covered;
    uint32_t stored, spc, bps;
    uint64_t chunk;
    if (data_size >= L01_VOLUME_SIZE) covered = 1048U;
    else if (data_size >= L01_VOLUME_SMALL_SIZE) covered = 90U;
    else return;
    if (!l01_read_at(device, data_at, volume, covered + 4U)) return;
    stored = xx_data_get_u32(volume, covered + 4U, covered, false);
    if (stored != 0U && xx_adler32(volume, covered) != stored) return;
    spc = xx_data_get_u32(volume, covered + 4U, 8U, false);
    bps = xx_data_get_u32(volume, covered + 4U, 12U, false);
    chunk = (uint64_t)spc * (uint64_t)bps;
    if (spc == 0U || bps == 0U || chunk > L01_MAX_CHUNK_SIZE) return;
    p->chunk_size = (uint32_t)chunk;
}

static bool l01_chunks_reserve(l01_parsed *p, size_t more)
{
    size_t wanted, capacity;
    l01_chunk *grown;
    if (more > (size_t)L01_MAX_CHUNKS - p->chunk_count) return false;
    wanted = p->chunk_count + more;
    if (wanted <= p->chunk_capacity) return true;
    capacity = p->chunk_capacity ? p->chunk_capacity : 256U;
    while (capacity < wanted) capacity *= 2U;
    grown = (l01_chunk *)xx_mem_realloc(p->chunks, capacity * sizeof(*grown));
    if (!grown) return false;
    p->chunks = grown;
    p->chunk_capacity = capacity;
    return true;
}

/* Appends the chunks one table lists. False leaves the list unchanged. */
static bool l01_read_table(xx_io_device *device, l01_parsed *p, int64_t base, int64_t data_at, uint64_t data_size, const l01_range *sectors)
{
    uint8_t header[L01_TABLE_HEADER_SIZE];
    uint8_t *raw = NULL;
    uint32_t entries, index;
    uint64_t table_base, array_size;
    int64_t origin, previous = -1;
    l01_range own, region = {-1, -1};
    bool has_footer, result = false;

    if (data_size < L01_TABLE_HEADER_SIZE || !l01_read_at(device, data_at, header, sizeof(header)) ||
        xx_adler32(header, 20U) != xx_data_get_u32(header, sizeof(header), 20U, false))
        return false;
    entries = xx_data_get_u32(header, sizeof(header), 0U, false);
    table_base = xx_data_get_u64(header, sizeof(header), 8U, false);
    array_size = (uint64_t)entries * 4U;
    if (array_size > data_size - L01_TABLE_HEADER_SIZE || entries > L01_MAX_CHUNKS) return false;
    if (entries == 0U) return true;
    if (table_base > (uint64_t)INT64_MAX - UINT64_C(0x100000000) || (uint64_t)base > (uint64_t)INT64_MAX - UINT64_C(0x100000000) - table_base) return false;
    origin = base + (int64_t)table_base;
    has_footer = data_size - L01_TABLE_HEADER_SIZE - array_size >= 4U;
    own.start = data_at + (int64_t)L01_TABLE_HEADER_SIZE + (int64_t)array_size + (has_footer ? 4 : 0);
    own.end = data_at + (int64_t)data_size;
    if (!l01_chunks_reserve(p, entries)) return false;
    raw = (uint8_t *)xx_mem_alloc((size_t)array_size + 4U);
    if (!raw || !l01_read_at(device, data_at + (int64_t)L01_TABLE_HEADER_SIZE, raw, (size_t)array_size + (has_footer ? 4U : 0U))) goto done;
    if (has_footer && xx_adler32(raw, (size_t)array_size) != xx_data_get_u32(raw, (size_t)array_size + 4U, (size_t)array_size, false)) goto done;
    for (index = 0U; index < entries; ++index) {
        uint32_t value = xx_data_get_u32(raw, (size_t)array_size, (size_t)index * 4U, false);
        int64_t at = origin + (int64_t)(value & 0x7FFFFFFFU);
        int64_t end;
        if (at <= previous) goto done;
        if (region.start < 0) {
            if (sectors && at >= sectors->start && at < sectors->end) region = *sectors;
            else if (at >= own.start && at < own.end) region = own;
            else goto done;
        }
        if (at < region.start || at >= region.end) goto done;
        if (index + 1U < entries) {
            uint32_t following = xx_data_get_u32(raw, (size_t)array_size, (size_t)(index + 1U) * 4U, false);
            end = origin + (int64_t)(following & 0x7FFFFFFFU);
            if (end > region.end) goto done;
        } else {
            end = region.end;
        }
        if (end <= at || (uint64_t)(end - at) > UINT32_MAX) goto done;
        p->chunks[p->chunk_count + index].at = at;
        p->chunks[p->chunk_count + index].length = (uint32_t)(end - at);
        p->chunks[p->chunk_count + index].compressed = (value & 0x80000000U) != 0U ? 1U : 0U;
        previous = at;
    }
    p->chunk_count += entries;
    result = true;
done:
    if (raw) xx_mem_free(raw);
    return result;
}

static bool l01_read_ltree(xx_io_device *device, l01_parsed *p, int64_t data_at, uint64_t data_size)
{
    uint8_t header[L01_LTREE_HEADER_SIZE];
    uint32_t stored;
    uint64_t text_size;
    if (data_size < L01_LTREE_HEADER_SIZE || !l01_read_at(device, data_at, header, sizeof(header))) return false;
    stored = xx_data_get_u32(header, sizeof(header), 24U, false);
    header[24] = header[25] = header[26] = header[27] = 0U;
    if (stored != 0U && xx_adler32(header, sizeof(header)) != stored) return false;
    text_size = xx_data_get_u64(header, sizeof(header), 16U, false);
    if (text_size < 2U || text_size > data_size - L01_LTREE_HEADER_SIZE || text_size > L01_MAX_LTREE_BYTES) return false;
    text_size &= ~UINT64_C(1);
    p->text = (uint8_t *)xx_mem_alloc((size_t)text_size);
    if (!p->text) return false;
    if (!l01_read_at(device, data_at + (int64_t)L01_LTREE_HEADER_SIZE, p->text, (size_t)text_size)) {
        xx_mem_free(p->text);
        p->text = NULL;
        return false;
    }
    p->text_units = (size_t)(text_size / 2U);
    return true;
}

/* ---------------------------------------------------------- ltree text -- */

typedef struct l01_line_s {
    size_t at;
    size_t length;
} l01_line;

static bool l01_next_line(const l01_parsed *p, size_t *position, l01_line *line)
{
    size_t at = *position, end;
    if (at >= p->text_units) return false;
    for (end = at; end < p->text_units && l01_unit(p, end) != 0x0AU; ++end) {
    }
    line->at = at;
    line->length = end - at;
    if (line->length != 0U && l01_unit(p, end - 1U) == 0x0DU) --line->length;
    *position = end < p->text_units ? end + 1U : end;
    return true;
}

static bool l01_equals(const l01_parsed *p, size_t at, size_t length, const char *word)
{
    size_t index;
    for (index = 0U; index < length; ++index)
        if (!word[index] || l01_unit(p, at + index) != (uint8_t)word[index]) return false;
    return word[length] == 0;
}

/* Decimal, optionally signed. Empty gives *empty = true. */
static bool l01_decimal(const l01_parsed *p, size_t at, size_t length, bool allow_sign, int64_t *value, bool *empty)
{
    uint64_t result = 0U;
    bool negative = false;
    size_t index = 0U;
    *empty = length == 0U;
    if (length == 0U) return true;
    if (allow_sign && l01_unit(p, at) == '-') {
        negative = true;
        index = 1U;
        if (length == 1U) return false;
    }
    for (; index < length; ++index) {
        uint32_t c = l01_unit(p, at + index);
        if (c < '0' || c > '9') return false;
        if (result > (UINT64_C(0x7FFFFFFFFFFFFFFF) - (c - '0')) / 10U) return false;
        result = result * 10U + (c - '0');
    }
    *value = negative ? -(int64_t)result : (int64_t)result;
    return true;
}

/* "<a>\t<b>" with both decimal. */
static bool l01_counts(const l01_parsed *p, const l01_line *line, uint32_t *second)
{
    size_t tab;
    int64_t a, b;
    bool empty;
    for (tab = 0U; tab < line->length; ++tab)
        if (l01_unit(p, line->at + tab) == 0x09U) break;
    if (tab == 0U || tab >= line->length) return false;
    if (!l01_decimal(p, line->at, tab, false, &a, &empty) || empty || !l01_decimal(p, line->at + tab + 1U, line->length - tab - 1U, false, &b, &empty) || empty ||
        b > 0x7FFFFFFF)
        return false;
    *second = (uint32_t)b;
    return true;
}

typedef struct l01_columns_s {
    uint32_t p, n, ls, be, opr, du;
} l01_columns;

#define L01_NO_COLUMN UINT32_MAX

static bool l01_read_types(const l01_parsed *p, const l01_line *line, l01_columns *columns)
{
    size_t start = 0U, index;
    uint32_t column = 0U;
    columns->p = columns->n = columns->ls = columns->be = columns->opr = columns->du = L01_NO_COLUMN;
    for (index = 0U; index <= line->length; ++index) {
        size_t at, length;
        if (index < line->length && l01_unit(p, line->at + index) != 0x09U) continue;
        at = line->at + start;
        length = index - start;
#define L01_TYPE(field, word) \
    if (columns->field == L01_NO_COLUMN && l01_equals(p, at, length, word)) columns->field = column;
        L01_TYPE(p, "p")
        L01_TYPE(n, "n")
        L01_TYPE(ls, "ls")
        L01_TYPE(be, "be")
        L01_TYPE(opr, "opr")
        L01_TYPE(du, "du")
#undef L01_TYPE
        if (++column > L01_MAX_TYPES) return false;
        start = index + 1U;
    }
    return columns->n != L01_NO_COLUMN;
}

static bool l01_entries_reserve(l01_parsed *p)
{
    size_t capacity;
    l01_entry *grown;
    if (p->entry_count < p->entry_capacity) return true;
    if (p->entry_count >= L01_MAX_ENTRIES) return false;
    capacity = p->entry_capacity ? p->entry_capacity * 2U : 64U;
    if (capacity > L01_MAX_ENTRIES) capacity = L01_MAX_ENTRIES;
    grown = (l01_entry *)xx_mem_realloc(p->entries, capacity * sizeof(*grown));
    if (!grown) return false;
    p->entries = grown;
    p->entry_capacity = capacity;
    return true;
}

static void l01_read_values(const l01_parsed *p, const l01_line *line, const l01_columns *columns, l01_entry *entry)
{
    size_t start = 0U, index;
    uint32_t column = 0U;
    entry->name_at = (uint32_t)line->at;
    entry->name_len = 0U;
    entry->be_at = 0U;
    entry->be_len = 0U;
    entry->flags = 0U;
    entry->size = 0U;
    entry->du = -1;
    entry->dir = 0U;
    entry->bad = 0U;
    for (index = 0U; index <= line->length; ++index) {
        size_t at, length;
        int64_t value;
        bool empty;
        if (index < line->length && l01_unit(p, line->at + index) != 0x09U) continue;
        at = line->at + start;
        length = index - start;
        if (column == columns->n) {
            entry->name_at = (uint32_t)at;
            entry->name_len = (uint32_t)length;
        } else if (column == columns->p) {
            if (l01_equals(p, at, length, "1")) entry->dir = 1U;
        } else if (column == columns->ls) {
            if (!l01_decimal(p, at, length, false, &value, &empty)) entry->bad = 1U;
            else if (!empty) entry->size = (uint64_t)value;
        } else if (column == columns->be) {
            entry->be_at = (uint32_t)at;
            entry->be_len = (uint32_t)length;
        } else if (column == columns->opr) {
            if (l01_decimal(p, at, length, false, &value, &empty) && !empty && value <= (int64_t)UINT32_MAX) entry->flags = (uint32_t)value;
        } else if (column == columns->du) {
            if (!l01_decimal(p, at, length, true, &value, &empty)) entry->bad = 1U;
            else if (!empty && value >= 0) entry->du = value;
        }
        ++column;
        start = index + 1U;
    }
}

/* The tree below the category root, entries in pre-order. */
static bool l01_read_tree(l01_parsed *p, size_t position, xx_pd_struct *pd)
{
    struct {
        uint32_t remaining;
        uint32_t parent;
    } stack[L01_MAX_DEPTH];
    size_t depth = 0U;
    l01_line line;
    l01_columns columns;
    uint32_t children;

    if (!l01_next_line(p, &position, &line) ||                                        /* "<count>\t1" */
        !l01_counts(p, &line, &children) || !l01_next_line(p, &position, &line) ||    /* the type line */
        !l01_read_types(p, &line, &columns) || !l01_next_line(p, &position, &line) || /* category root */
        !l01_counts(p, &line, &children) || !l01_next_line(p, &position, &line))
        return false;
    stack[0].remaining = children;
    stack[0].parent = L01_NO_PARENT;
    depth = 1U;
    while (depth != 0U) {
        l01_entry *entry;
        uint32_t sub;
        if (stack[depth - 1U].remaining == 0U) {
            --depth;
            continue;
        }
        if (pd && xx_pd_is_stopped(pd)) return false;
        --stack[depth - 1U].remaining;
        if (!l01_next_line(p, &position, &line) || !l01_counts(p, &line, &sub) || !l01_next_line(p, &position, &line) || !l01_entries_reserve(p)) return false;
        entry = &p->entries[p->entry_count];
        l01_read_values(p, &line, &columns, entry);
        entry->parent = stack[depth - 1U].parent;
        if (sub != 0U) entry->dir = 1U;
        if (sub != 0U) {
            if (depth >= L01_MAX_DEPTH) return false;
            stack[depth].remaining = sub;
            stack[depth].parent = (uint32_t)p->entry_count;
            ++depth;
        }
        ++p->entry_count;
    }
    return true;
}

static bool l01_parse_ltree(l01_parsed *p, xx_pd_struct *pd)
{
    size_t position = 0U;
    l01_line line;
    while (l01_next_line(p, &position, &line)) {
        size_t after = position;
        if (!l01_equals(p, line.at, line.length, "entry")) continue;
        p->entry_count = 0U;
        if (l01_read_tree(p, after, pd)) return true;
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    return false;
}

/* ----------------------------------------------------------------- walk -- */

static bool l01_walk(Abstractformat *self, l01_parsed *p, bool load_tree, xx_pd_struct *pd)
{
    xx_io_device *device = self->device;
    int64_t base = self->base_address;
    int64_t at;
    uint32_t sections = 0U;
    l01_range sectors = {-1, -1};
    bool have_sectors = false;
    bool table_ok = false;     /* the table before a table2 was usable */
    bool table_failed = false; /* a damaged table still needs its table2 */

    xx_mem_zero(p, sizeof(*p));
    p->input_size = xx_io_total_size(device);
    if (p->input_size < 0 || base < 0 || !l01_read_file_header(device, p->input_size, base, &p->segment)) return false;
    at = base + L01_FILE_HEADER_SIZE;
    p->format_end = at;

    for (;;) {
        l01_descriptor d;
        int64_t relative = at - base;
        int64_t data_at = at + L01_DESCRIPTOR_SIZE;
        int64_t data_end;
        uint64_t data_size;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (++sections > L01_MAX_SECTIONS) break;
        /* A chain that stops short (truncated copy) ends the walk; what was
         * read so far is still usable. The first descriptor must be good. */
        if (!l01_read_descriptor(device, p->input_size, at, &d)) {
            if (sections == 1U) return false;
            break;
        }
        if (d.kind == L01_SECTION_NEXT || d.kind == L01_SECTION_DONE) {
            if (d.next != (uint64_t)relative && d.next != (uint64_t)relative + L01_DESCRIPTOR_SIZE) {
                if (sections == 1U) return false;
                break;
            }
            p->format_end = data_at;
            p->finished = d.kind == L01_SECTION_DONE;
            break;
        }
        if (d.next < (uint64_t)relative + L01_DESCRIPTOR_SIZE || d.next > (uint64_t)(p->input_size - base)) {
            if (sections == 1U) return false;
            break;
        }
        if (d.size >= L01_DESCRIPTOR_SIZE && l01_within(p->input_size, at, d.size)) data_end = at + (int64_t)d.size;
        else data_end = base + (int64_t)d.next;
        data_size = data_end > data_at ? (uint64_t)(data_end - data_at) : 0U;
        if (data_end > p->format_end) p->format_end = data_end;
        /* A damaged table not followed by its mirror leaves a hole in the
         * media: nothing after it can be placed. */
        if (table_failed && d.kind != L01_SECTION_TABLE2) {
            p->chunks_broken = true;
            table_failed = false;
        }

        switch (d.kind) {
            case L01_SECTION_VOLUME:
                if (p->chunk_size == 0U) l01_read_geometry(device, p, data_at, data_size);
                break;
            case L01_SECTION_SECTORS:
                sectors.start = data_at;
                sectors.end = data_at + (int64_t)data_size;
                have_sectors = true;
                break;
            case L01_SECTION_TABLE:
                table_ok = p->chunks_broken || l01_read_table(device, p, base, data_at, data_size, have_sectors ? &sectors : NULL);
                table_failed = !table_ok;
                break;
            case L01_SECTION_TABLE2:
                /* A mirror of the table before it: only needed when that
                 * one was damaged. When both are, the chunk list stops. */
                if (!table_ok && !p->chunks_broken && !l01_read_table(device, p, base, data_at, data_size, have_sectors ? &sectors : NULL)) p->chunks_broken = true;
                table_ok = true;
                table_failed = false;
                break;
            case L01_SECTION_LTREE:
                if (load_tree && !p->has_ltree) {
                    if (!l01_read_ltree(device, p, data_at, data_size) || !l01_parse_ltree(p, pd)) return false;
                    p->has_ltree = true;
                }
                break;
            default: break;
        }
        at = base + (int64_t)d.next;
    }
    if (table_failed) p->chunks_broken = true;
    return true;
}

/* ------------------------------------------------------------- members -- */

static void l01_put_utf8(char *out, size_t *at, uint32_t cp)
{
    if (cp < 0x80U) {
        out[(*at)++] = (char)cp;
    } else if (cp < 0x800U) {
        out[(*at)++] = (char)(0xC0U | (cp >> 6));
        out[(*at)++] = (char)(0x80U | (cp & 0x3FU));
    } else if (cp < 0x10000U) {
        out[(*at)++] = (char)(0xE0U | (cp >> 12));
        out[(*at)++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[(*at)++] = (char)(0x80U | (cp & 0x3FU));
    } else {
        out[(*at)++] = (char)(0xF0U | (cp >> 18));
        out[(*at)++] = (char)(0x80U | ((cp >> 12) & 0x3FU));
        out[(*at)++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[(*at)++] = (char)(0x80U | (cp & 0x3FU));
    }
}

/* Append one entry name. Path separators and drive colons inside a name
 * become '_'; an unpaired surrogate becomes U+FFFD; an empty name is
 * "NoName", as EnCase shows it. */
static bool l01_put_component(const l01_parsed *p, const l01_entry *e, char *out, size_t *at)
{
    size_t index;
    if (e->name_len == 0U) {
        if (*at + 6U > L01_MAX_PATH) return false;
        xx_rt_memcpy(out + *at, "NoName", 6U);
        *at += 6U;
        return true;
    }
    for (index = 0U; index < e->name_len; ++index) {
        uint32_t cp = l01_unit(p, (size_t)e->name_at + index);
        if (*at + 4U > L01_MAX_PATH) return false;
        if (cp >= 0xD800U && cp <= 0xDBFFU && index + 1U < e->name_len) {
            uint32_t low = l01_unit(p, (size_t)e->name_at + index + 1U);
            if (low >= 0xDC00U && low <= 0xDFFFU) {
                cp = 0x10000U + ((cp - 0xD800U) << 10) + (low - 0xDC00U);
                ++index;
            } else {
                cp = 0xFFFDU;
            }
        } else if (cp >= 0xD800U && cp <= 0xDFFFU) {
            cp = 0xFFFDU;
        } else if (cp == '/' || cp == '\\' || cp == ':') {
            cp = '_';
        }
        l01_put_utf8(out, at, cp);
    }
    return true;
}

/* '/'-joined path of an entry into out (L01_MAX_PATH + 1 bytes). Returns the
 * length, 0 when the path is too long. */
static size_t l01_build_path(const l01_parsed *p, uint32_t entry, char *out)
{
    uint32_t chain[L01_MAX_DEPTH + 1U];
    size_t depth = 0U, at = 0U;
    uint32_t e = entry;
    while (e != L01_NO_PARENT && e < p->entry_count && depth <= L01_MAX_DEPTH) {
        chain[depth++] = e;
        e = p->entries[e].parent;
    }
    if (e != L01_NO_PARENT) return 0U;
    while (depth != 0U) {
        --depth;
        if (!l01_put_component(p, &p->entries[chain[depth]], out, &at)) return 0U;
        if (depth != 0U) {
            if (at + 1U > L01_MAX_PATH) return 0U;
            out[at++] = '/';
        }
    }
    out[at] = 0;
    return at;
}

static uint64_t l01_hash(const char *name, size_t length)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* "%_<index>" before the extension of the last component. */
static size_t l01_insert_suffix(char *name, size_t length, uint32_t index)
{
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, count = 0U, component = 0U, at, dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (count != 0U) suffix[suffix_length++] = digits[--count];
    for (at = length + 1U; at > dot; --at) name[at - 1U + suffix_length] = name[at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
    return length + suffix_length;
}

static size_t l01_member_name(const l01_parsed *p, size_t file, char *out)
{
    size_t length = l01_build_path(p, p->files[file].entry, out);
    if (length != 0U && p->files[file].renamed) length = l01_insert_suffix(out, length, (uint32_t)file);
    return length;
}

typedef struct l01_key_s {
    uint64_t hash;
    uint32_t index;
} l01_key;

static int l01_compare_keys(const void *left, const void *right)
{
    const l01_key *a = (const l01_key *)left;
    const l01_key *b = (const l01_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Files whose paths collide (ASCII case folded) are renamed, all but the
 * first; a renamed path that still collides is blocked from extraction. */
static bool l01_resolve_names(l01_parsed *p, char *buffer)
{
    l01_key *keys;
    size_t index, round;
    if (p->file_count < 2U) return true;
    keys = (l01_key *)xx_mem_alloc(p->file_count * sizeof(*keys));
    if (!keys) return false;
    for (round = 0U; round < 2U; ++round) {
        for (index = 0U; index < p->file_count; ++index) {
            size_t length = l01_member_name(p, index, buffer);
            keys[index].hash = l01_hash(buffer, length);
            keys[index].index = (uint32_t)index;
        }
        xx_rt_qsort(keys, p->file_count, sizeof(*keys), l01_compare_keys);
        for (index = 1U; index < p->file_count; ++index) {
            if (keys[index].hash != keys[index - 1U].hash) continue;
            if (round == 0U) p->files[keys[index].index].renamed = 1U;
            else p->files[keys[index].index].blocked = 1U;
        }
    }
    xx_mem_free(keys);
    return true;
}

static bool l01_collect_files(l01_parsed *p, char *buffer)
{
    size_t index, count = 0U;
    for (index = 0U; index < p->entry_count; ++index)
        if (!p->entries[index].dir) ++count;
    if (count == 0U) return true;
    p->files = (l01_file *)xx_mem_calloc(count, sizeof(*p->files));
    if (!p->files) return false;
    for (index = 0U; index < p->entry_count; ++index)
        if (!p->entries[index].dir) p->files[p->file_count++].entry = (uint32_t)index;
    return l01_resolve_names(p, buffer);
}

static bool l01_parse(Abstractformat *self, l01_parsed *p, char *buffer, xx_pd_struct *pd)
{
    if (!self || !l01_walk(self, p, true, pd) || !l01_collect_files(p, buffer)) {
        l01_parsed_free(p);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------- extraction -- */

static bool l01_decode_chunk(Abstractformat *self, l01_stream *s, size_t index, xx_pd_struct *pd)
{
    const l01_parsed *p = &s->parsed;
    const l01_chunk *chunk;
    size_t length, consumed = 0U, trailer;
    int64_t produced;
    bool last;
    if (s->cached == index) return true;
    s->cached = SIZE_MAX;
    if (index >= p->chunk_count || p->chunk_size == 0U) return false;
    chunk = &p->chunks[index];
    last = index + 1U == p->chunk_count;
    length = chunk->length;
    if (length > s->packed_capacity) return false;
    if (!chunk->compressed) {
        size_t stored;
        if (length < 5U) return false;
        stored = length - 4U;
        if (stored > p->chunk_size) stored = p->chunk_size;
        if ((!last && stored != p->chunk_size) || !l01_read_at(self->device, chunk->at, s->chunk, stored + 4U) ||
            xx_adler32(s->chunk, stored) != xx_data_get_u32(s->chunk, stored + 4U, stored, false))
            return false;
        s->cached = index;
        s->cached_length = stored;
        return true;
    }
    if (length < 6U || !l01_read_at(self->device, chunk->at, s->packed, length) || !xx_zlib_stream_header_is_valid(s->packed, length) ||
        xx_io_seek64(s->chunk_device, 0, SEEK_SET) != 0)
        return false;
    /* The memory device is exactly one chunk long: a stream that would
     * inflate past it fails on the write instead of growing anything. */
    if (!xx_deflate_unpack_memory_to_device_ex(s->packed + 2, length - 2U, s->chunk_device, &consumed, false, pd)) return false;
    produced = xx_io_tell(s->chunk_device);
    if (produced <= 0 || (uint64_t)produced > p->chunk_size || (!last && (uint64_t)produced != p->chunk_size)) return false;
    trailer = 2U + consumed;
    if (trailer > length || length - trailer < 4U || xx_data_get_u32(s->packed, length, trailer, true) != xx_adler32(s->chunk, (size_t)produced)) return false;
    s->cached = index;
    s->cached_length = (size_t)produced;
    return true;
}

static bool l01_prepare_decoder(l01_stream *s)
{
    const l01_parsed *p = &s->parsed;
    if (s->chunk) return true;
    if (p->chunk_size == 0U) return false;
    s->packed_capacity = (size_t)p->chunk_size * 2U + 1024U;
    if ((uint64_t)s->packed_capacity > (uint64_t)p->input_size) s->packed_capacity = (size_t)p->input_size;
    s->chunk = (uint8_t *)xx_mem_alloc((size_t)p->chunk_size + 4U);
    s->packed = (uint8_t *)xx_mem_alloc(s->packed_capacity + 1U);
    if (!s->chunk || !s->packed) return false;
    s->chunk_device = xx_io_mem_open(s->chunk, p->chunk_size);
    return s->chunk_device != NULL;
}

/* Media bytes [offset, offset + size) to output (NULL: decode only). */
static bool l01_copy_media(Abstractformat *self, l01_stream *s, uint64_t offset, uint64_t size, xx_io_device *output, xx_pd_struct *pd)
{
    const l01_parsed *p = &s->parsed;
    if (size == 0U) return true;
    if (!l01_prepare_decoder(s)) return false;
    if (offset > (uint64_t)p->chunk_count * p->chunk_size || size > (uint64_t)p->chunk_count * p->chunk_size - offset) return false;
    while (size != 0U) {
        size_t index = (size_t)(offset / p->chunk_size);
        size_t within = (size_t)(offset % p->chunk_size);
        size_t take;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!l01_decode_chunk(self, s, index, pd) || within >= s->cached_length) return false;
        take = s->cached_length - within;
        if ((uint64_t)take > size) take = (size_t)size;
        if (output && !l01_write_all(output, s->chunk + within, take)) return false;
        offset += take;
        size -= take;
    }
    return true;
}

typedef struct l01_extent_s {
    uint64_t offset;
    uint64_t size;
} l01_extent;

/* Next space-separated token of the "be" value. */
static bool l01_token(const l01_parsed *p, const l01_entry *e, size_t *at, size_t *start, size_t *length)
{
    size_t end = (size_t)e->be_len;
    while (*at < end && l01_unit(p, (size_t)e->be_at + *at) == ' ') ++*at;
    if (*at >= end) return false;
    *start = (size_t)e->be_at + *at;
    while (*at < end && l01_unit(p, (size_t)e->be_at + *at) != ' ') ++*at;
    *length = (size_t)e->be_at + *at - *start;
    return true;
}

static bool l01_hex(const l01_parsed *p, size_t at, size_t length, uint64_t *value)
{
    uint64_t result = 0U;
    size_t index;
    if (length == 0U || length > 16U) return false;
    for (index = 0U; index < length; ++index) {
        uint32_t c = l01_unit(p, at + index), digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10U;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10U;
        else return false;
        result = (result << 4) | digit;
    }
    *value = result;
    return true;
}

/* Walks the extents of an entry. With output == NULL and verify only the
 * syntax and the sum are checked (sum returned in *total). */
static bool l01_extents(Abstractformat *self, l01_stream *s, const l01_entry *e, bool first_only, l01_extent *first, uint64_t *total, xx_io_device *output, bool copy,
                        xx_pd_struct *pd)
{
    const l01_parsed *p = &s->parsed;
    size_t at = 0U, start, length;
    uint64_t count, index, sum = 0U;
    if (!l01_token(p, e, &at, &start, &length) || !l01_hex(p, start, length, &count) || count == 0U || count > (uint64_t)e->be_len) return false;
    for (index = 0U; index < count; ++index) {
        l01_extent extent;
        if (!l01_token(p, e, &at, &start, &length)) return false;
        if (length == 1U && l01_unit(p, start) == 'S' && !l01_token(p, e, &at, &start, &length)) return false;
        if (!l01_hex(p, start, length, &extent.offset) || !l01_token(p, e, &at, &start, &length) || !l01_hex(p, start, length, &extent.size) ||
            extent.size > UINT64_MAX - sum)
            return false;
        sum += extent.size;
        if (index == 0U && first) *first = extent;
        if (copy && !l01_copy_media(self, s, extent.offset, extent.size, output, pd)) return false;
        if (first_only) break;
    }
    if (total) *total = sum;
    return true;
}

static bool l01_fill(xx_io_device *output, uint8_t value, uint64_t size, xx_pd_struct *pd)
{
    uint8_t *buffer;
    bool result = true;
    if (!output || size == 0U) return true;
    buffer = (uint8_t *)xx_mem_alloc(L01_FILL_BUFFER);
    if (!buffer) return false;
    xx_rt_memset(buffer, value, L01_FILL_BUFFER);
    while (size != 0U && result) {
        size_t take = size > L01_FILL_BUFFER ? L01_FILL_BUFFER : (size_t)size;
        if (pd && xx_pd_is_stopped(pd)) result = false;
        else result = l01_write_all(output, buffer, take);
        size -= take;
    }
    xx_mem_free(buffer);
    return result;
}

static bool l01_unpack_entry(Abstractformat *self, l01_stream *s, const l01_entry *e, xx_io_device *output, xx_pd_struct *pd)
{
    l01_extent first;
    uint64_t total;
    if (e->bad) return false;
    if (e->size == 0U) return true;
    if (e->flags & L01_FLAG_SPARSE) {
        if (e->du >= 0) return l01_copy_media(self, s, (uint64_t)e->du, e->size, output, pd);
        /* One stored byte, repeated. */
        if (e->size > L01_MAX_SPARSE_SIZE || !l01_extents(self, s, e, true, &first, &total, NULL, false, pd) || first.size == 0U || !l01_prepare_decoder(s)) return false;
        {
            uint8_t value;
            size_t index, within;
            if (first.offset >= (uint64_t)s->parsed.chunk_count * s->parsed.chunk_size) return false;
            index = (size_t)(first.offset / s->parsed.chunk_size);
            within = (size_t)(first.offset % s->parsed.chunk_size);
            if (!l01_decode_chunk(self, s, index, pd) || within >= s->cached_length) return false;
            value = s->chunk[within];
            return l01_fill(output, value, e->size, pd);
        }
    }
    if (!l01_extents(self, s, e, false, NULL, &total, NULL, false, pd) || total != e->size) return false;
    return l01_extents(self, s, e, false, NULL, NULL, output, true, pd);
}

static bool l01_stored_size(l01_stream *s, const l01_entry *e, uint64_t *stored)
{
    uint64_t total = 0U;
    size_t at = 0U, start, length;
    uint64_t count, index;
    const l01_parsed *p = &s->parsed;
    *stored = 0U;
    if (!l01_token(p, e, &at, &start, &length) || !l01_hex(p, start, length, &count) || count > (uint64_t)e->be_len) return false;
    for (index = 0U; index < count; ++index) {
        uint64_t offset, size;
        if (!l01_token(p, e, &at, &start, &length)) return false;
        if (length == 1U && l01_unit(p, start) == 'S' && !l01_token(p, e, &at, &start, &length)) return false;
        if (!l01_hex(p, start, length, &offset) || !l01_token(p, e, &at, &start, &length) || !l01_hex(p, start, length, &size) || size > UINT64_MAX - total) return false;
        total += size;
    }
    *stored = total;
    return true;
}

/* ----------------------------------------------------------- name safety -- */

static bool l01_reserved_component(const char *segment, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ') --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') || (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length && xx_rt_memcmp(stem, devices[index], stem_length) == 0) return true;
    return false;
}

/* Relative, no "." / ".." or trailing dot/space components (Windows would
 * alias them), no reserved device names, no characters Windows refuses. */
static bool l01_safe_name(const char *name)
{
    const char *segment, *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' || c == 0x7FU || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || l01_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ------------------------------------------------------------- records -- */

static void l01_stream_free(void *opaque)
{
    l01_stream *s = (l01_stream *)opaque;
    if (!s) return;
    if (s->chunk_device) xx_io_close(s->chunk_device);
    if (s->chunk) xx_mem_free(s->chunk);
    if (s->packed) xx_mem_free(s->packed);
    if (s->name) xx_mem_free(s->name);
    l01_parsed_free(&s->parsed);
    xx_mem_free(s);
}

static const xx_var *l01_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool l01_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static bool l01_size_allowed(Abstractformat *format, const xx_list_s *options, uint64_t size)
{
    const xx_var *limit = xx_format_resolve_extra_parameter(format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return true;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return size <= xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value < 0 || size <= (uint64_t)value;
        }
        default: return true;
    }
}

static bool l01_set_record(xx_archive_record *record, l01_stream *s)
{
    const l01_entry *e = &s->parsed.entries[s->parsed.files[s->index].entry];
    uint64_t stored = 0U;
    size_t length = l01_member_name(&s->parsed, s->index, s->name);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (length == 0U) {
        xx_rt_memcpy(s->name, "NoName", 7U);
    }
    (void)l01_stored_size(s, e, &stored);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = -1;
    record->compressed_size = (int64_t)(stored > (uint64_t)INT64_MAX ? (uint64_t)INT64_MAX : stored);
    return xx_archive_record_set_original_name(record, s->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, e->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, stored) && xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, e->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------- public API -- */

void xx_ewf_l01_init(xx_ewf_l01 *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_EWF_L01_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ewf-l01");
    xx_format_set_extension(&archive->format, "L01");
    archive->format.check_is_valid = xx_ewf_l01_check_is_valid;
    archive->format.handle_base_info = xx_ewf_l01_handle_base_info;
    archive->format.get_format_size = xx_ewf_l01_get_format_size;
    archive->format.get_number_of_archive_records = xx_ewf_l01_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_ewf_l01_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_ewf_l01_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_ewf_l01_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_ewf_l01_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_ewf_l01_free_archive_records_reading;
}

xx_ewf_l01 *xx_ewf_l01_create(xx_io_device *device, int64_t base_address)
{
    xx_ewf_l01 *archive = (xx_ewf_l01 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ewf_l01_init(archive, device, base_address);
    return archive;
}

void xx_ewf_l01_destroy(xx_ewf_l01 *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ewf_l01_free(xx_ewf_l01 *archive)
{
    if (!archive) return;
    xx_ewf_l01_destroy(archive);
    xx_mem_free(archive);
}

/* The probe: the 13-byte file header and one checksummed descriptor that
 * points forward inside the file. Two small reads, no allocation. */
bool xx_ewf_l01_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    int64_t input_size, at;
    l01_descriptor d;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) return false;
    input_size = xx_io_total_size(self->device);
    if (!l01_read_file_header(self->device, input_size, self->base_address, NULL)) return false;
    at = self->base_address + L01_FILE_HEADER_SIZE;
    if (!l01_read_descriptor(self->device, input_size, at, &d)) return false;
    if (d.kind == L01_SECTION_DONE || d.kind == L01_SECTION_NEXT) return d.next == L01_FILE_HEADER_SIZE || d.next == L01_FILE_HEADER_SIZE + L01_DESCRIPTOR_SIZE;
    return d.next >= (uint64_t)L01_FILE_HEADER_SIZE + L01_DESCRIPTOR_SIZE && d.next <= (uint64_t)(input_size - self->base_address);
}

bool xx_ewf_l01_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    l01_parsed parsed;
    xx_ewf_l01 *archive;
    char *buffer;
    bool ok;
    if (!self) return false;
    buffer = (char *)xx_mem_alloc(L01_MAX_PATH + 16U);
    if (!buffer) return false;
    ok = l01_parse(self, &parsed, buffer, pd);
    xx_mem_free(buffer);
    if (!ok) return false;
    archive = (xx_ewf_l01 *)self;
    archive->number_of_records = parsed.file_count;
    archive->number_of_entries = parsed.entry_count;
    archive->number_of_chunks = (uint32_t)parsed.chunk_count;
    archive->chunk_size = parsed.chunk_size;
    archive->media_size = (uint64_t)parsed.chunk_count * parsed.chunk_size;
    archive->segment_number = parsed.segment;
    archive->has_ltree = parsed.has_ltree;
    archive->finished = parsed.finished;
    self->number_of_archive_records = parsed.file_count;
    self->format_size = parsed.format_end - self->base_address;
    self->is_valid = true;
    self->base_info_handled = true;
    l01_parsed_free(&parsed);
    return true;
}

int64_t xx_ewf_l01_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_ewf_l01_handle_base_info(self, pd)) ? self->format_size : -1;
}

uint64_t xx_ewf_l01_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    return self && (self->base_info_handled || xx_ewf_l01_handle_base_info(self, pd)) ? ((xx_ewf_l01 *)self)->number_of_records : 0U;
}

xx_archive_record_state *xx_ewf_l01_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    l01_stream *s;
    xx_archive_record_state *state;
    if (!self) return NULL;
    s = (l01_stream *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return NULL;
    s->cached = SIZE_MAX;
    s->name = (char *)xx_mem_alloc(L01_MAX_PATH + 16U);
    if (!s->name || !l01_parse(self, &s->parsed, s->name, pd)) {
        if (s->name) xx_mem_free(s->name);
        xx_mem_free(s);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        l01_stream_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = s;
    state->free_internal = l01_stream_free;
    state->total_records = (int64_t)s->parsed.file_count;
    if (!l01_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (s->parsed.file_count != 0U) {
        if (!l01_set_record(&state->current_record, s)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_ewf_l01_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_ewf_l01_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    l01_stream *s;
    (void)pd;
    if (!self || !state || state->format != self || !(s = (l01_stream *)state->internal_state) || s->index + 1U >= s->parsed.file_count) {
        if (state) state->has_record = false;
        return false;
    }
    ++s->index;
    state->current_index = (int64_t)s->index;
    state->has_record = l01_set_record(&state->current_record, s);
    return state->has_record;
}

bool xx_ewf_l01_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    l01_stream *s;
    const l01_entry *e;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    size_t length;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record || !(s = (l01_stream *)state->internal_state) || s->index >= s->parsed.file_count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    e = &s->parsed.entries[s->parsed.files[s->index].entry];
    if (s->parsed.files[s->index].blocked || !l01_size_allowed(self, &state->options, e->size)) return false;
    path_option = l01_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return l01_unpack_entry(self, s, e, NULL, pd);
    length = l01_member_name(&s->parsed, s->index, s->name);
    if (length == 0U || !l01_safe_name(s->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", s->name) : xx_str_concat(base, s->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = l01_unpack_entry(self, s, e, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ewf_l01_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
