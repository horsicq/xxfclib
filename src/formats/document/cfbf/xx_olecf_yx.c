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


#include <xxfclib/formats/document/cfbf/xx_olecf_yx.h>

#include <stdlib.h>
#include <string.h>

#define XX_OLECF_YX_MAX_SECTOR UINT32_C(0xfffffffa)
#define XX_OLECF_YX_ENDOFCHAIN UINT32_C(0xfffffffe)
/* MAX_STREAM_SIZE of the reference parser. */
#define XX_OLECF_YX_MAX_STREAM_SIZE (UINT64_C(256) * 1024u * 1024u)

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t sector_size;
    size_t mini_sector_size;
    uint32_t *fat_sectors;
    size_t fat_count;
    size_t fat_capacity;
    uint32_t *mini_fat_sectors;
    size_t mini_fat_count;
    size_t mini_fat_capacity;
    /* The last Root Storage entry: the mini stream container. */
    uint32_t mini_stream_start;
    uint64_t mini_stream_size;
    uint32_t *visited_difat;
    size_t visited_difat_count;
    size_t visited_difat_capacity;
    uint32_t *directory_sectors;
    size_t directory_count;
    size_t directory_capacity;
    xx_olecf_yx_stream *streams;
    size_t stream_count;
    size_t stream_capacity;
} xx_olecf_yx_model;

static const uint8_t xx_olecf_yx_signature[8] = {
    0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1
};

static bool xx_olecf_yx_read16(const uint8_t *data, size_t size,
                             size_t at, uint16_t *value)
{
    if (!data || !value || at > size || size - at < 2) return false;
    *value = (uint16_t)((uint16_t)data[at] |
                         ((uint16_t)data[at + 1] << 8));
    return true;
}

static bool xx_olecf_yx_read32(const uint8_t *data, size_t size,
                             size_t at, uint32_t *value)
{
    if (!data || !value || at > size || size - at < 4) return false;
    *value = (uint32_t)data[at] |
             ((uint32_t)data[at + 1] << 8) |
             ((uint32_t)data[at + 2] << 16) |
             ((uint32_t)data[at + 3] << 24);
    return true;
}

static bool xx_olecf_yx_push_u32(uint32_t **items, size_t *count,
                               size_t *capacity, uint32_t value)
{
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 16;
        uint32_t *memory;
        if (next < *capacity || next > SIZE_MAX / sizeof(**items))
            return false;
        memory = realloc(*items, next * sizeof(**items));
        if (!memory) return false;
        *items = memory;
        *capacity = next;
    }
    (*items)[(*count)++] = value;
    return true;
}

static bool xx_olecf_yx_has_u32(const uint32_t *items, size_t count,
                              uint32_t value)
{
    for (size_t i = 0; i < count; ++i)
        if (items[i] == value) return true;
    return false;
}

static bool xx_olecf_yx_sector(const xx_olecf_yx_model *model, uint32_t sector,
                             const uint8_t **bytes)
{
    uint64_t offset = ((uint64_t)sector + 1u) * model->sector_size;
    if (offset > model->size || model->size - (size_t)offset <
                                model->sector_size) return false;
    *bytes = model->data + (size_t)offset;
    return true;
}

static bool xx_olecf_yx_fat_entry(const xx_olecf_yx_model *model,
                                uint32_t sector, uint32_t *next)
{
    size_t entries_per_sector = model->sector_size / 4;
    size_t fat_index = sector / entries_per_sector;
    const uint8_t *fat;
    if (fat_index >= model->fat_count ||
        !xx_olecf_yx_sector(model, model->fat_sectors[fat_index], &fat))
        return false;
    return xx_olecf_yx_read32(fat, model->sector_size,
                            (sector % entries_per_sector) * 4, next);
}

static bool xx_olecf_yx_append_utf8(char *output, size_t *length,
                                  uint32_t codepoint)
{
    uint8_t bytes[4];
    size_t count;
    if (codepoint <= 0x7f) {
        bytes[0] = (uint8_t)codepoint;
        count = 1;
    } else if (codepoint <= 0x7ff) {
        bytes[0] = (uint8_t)(0xc0 | (codepoint >> 6));
        bytes[1] = (uint8_t)(0x80 | (codepoint & 0x3f));
        count = 2;
    } else if (codepoint <= 0xffff) {
        bytes[0] = (uint8_t)(0xe0 | (codepoint >> 12));
        bytes[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | (codepoint & 0x3f));
        count = 3;
    } else if (codepoint <= 0x10ffff) {
        bytes[0] = (uint8_t)(0xf0 | (codepoint >> 18));
        bytes[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[3] = (uint8_t)(0x80 | (codepoint & 0x3f));
        count = 4;
    } else return false;
    if (*length > 127u - count) return false;
    memcpy(output + *length, bytes, count);
    *length += count;
    return true;
}

static bool xx_olecf_yx_decode_name(const uint8_t *entry,
                                  uint16_t byte_length,
                                  char output[128], size_t *length)
{
    size_t units = (byte_length / 2u) - 1u;
    size_t used = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t codepoint = (uint32_t)entry[i * 2] |
                             ((uint32_t)entry[i * 2 + 1] << 8);
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            uint32_t low;
            if (++i >= units) return false;
            low = (uint32_t)entry[i * 2] |
                  ((uint32_t)entry[i * 2 + 1] << 8);
            if (low < 0xdc00 || low > 0xdfff) return false;
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                        (low - 0xdc00);
        } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff)
            return false;
        if (!xx_olecf_yx_append_utf8(output, &used, codepoint)) return false;
    }
    if (used && (uint8_t)output[0] < 0x20) {
        memmove(output, output + 1, used - 1);
        --used;
    }
    output[used] = '\0';
    *length = used;
    return true;
}

static bool xx_olecf_yx_add_stream(xx_olecf_yx_model *model,
                                 const xx_olecf_yx_stream *stream)
{
    for (size_t i = 0; i < model->stream_count; ++i) {
        xx_olecf_yx_stream *existing = &model->streams[i];
        if (existing->name_length != stream->name_length ||
            memcmp(existing->name, stream->name,
                    stream->name_length) != 0) continue;
        if (stream->type == 2 || existing->type != 2)
            *existing = *stream;
        return true;
    }
    if (model->stream_count == model->stream_capacity) {
        size_t next = model->stream_capacity ?
                      model->stream_capacity * 2 : 16;
        xx_olecf_yx_stream *memory;
        if (next < model->stream_capacity ||
            next > SIZE_MAX / sizeof(*memory)) return false;
        memory = realloc(model->streams, next * sizeof(*memory));
        if (!memory) return false;
        model->streams = memory;
        model->stream_capacity = next;
    }
    model->streams[model->stream_count++] = *stream;
    return true;
}

static bool xx_olecf_yx_parse_entry(xx_olecf_yx_model *model,
                                  const uint8_t *entry)
{
    uint16_t name_length;
    xx_olecf_yx_stream stream;
    uint32_t size;
    if (!xx_olecf_yx_read16(entry, 128, 64, &name_length) ||
        name_length < 2 || name_length > 64) return true;
    if (entry[66] != 1 && entry[66] != 2 && entry[66] != 5)
        return true;
    memset(&stream, 0, sizeof(stream));
    if (!xx_olecf_yx_decode_name(entry, name_length,
                               stream.name, &stream.name_length) ||
        !xx_olecf_yx_read32(entry, 128, 116, &stream.start_sector) ||
        !xx_olecf_yx_read32(entry, 128, 120, &size)) return true;
    stream.type = entry[66];
    stream.size = size;
    /* The reference takes every parsed Root Storage entry as the mini
     * stream container, independently of the entry table below. */
    if (stream.type == 5) {
        model->mini_stream_start = stream.start_sector;
        model->mini_stream_size = stream.size;
    }
    return xx_olecf_yx_add_stream(model, &stream);
}

/* Olecf::follow_chain: FAT chain from `start` until a special sector, a
 * read failure or a cycle. False only on allocation failure. */
static bool xx_olecf_yx_follow_chain(const xx_olecf_yx_model *model, uint32_t start,
                                   uint32_t **items, size_t *count,
                                   size_t *capacity)
{
    uint32_t current = start;
    while (current < XX_OLECF_YX_MAX_SECTOR &&
           !xx_olecf_yx_has_u32(*items, *count, current)) {
        uint32_t next;
        if (!xx_olecf_yx_push_u32(items, count, capacity, current))
            return false;
        if (!xx_olecf_yx_fat_entry(model, current, &next) ||
            next >= XX_OLECF_YX_MAX_SECTOR) break;
        current = next;
    }
    return true;
}

static bool xx_olecf_yx_parse(const uint8_t *data, size_t size,
                            xx_olecf_yx_model *model)
{
    uint16_t byte_order, sector_shift, mini_sector_shift;
    uint32_t num_fat, first_dir, first_mini_fat, mini_fat_count;
    uint32_t first_difat, difat_count;
    size_t possible, entries_per_sector, max_fat_sectors;
    uint32_t next_difat;
    memset(model, 0, sizeof(*model));
    model->data = data;
    model->size = size;
    if (!data || size < 76 ||
        memcmp(data, xx_olecf_yx_signature, 8) != 0 ||
        !xx_olecf_yx_read16(data, size, 28, &byte_order) ||
        !xx_olecf_yx_read16(data, size, 30, &sector_shift) ||
        !xx_olecf_yx_read16(data, size, 32, &mini_sector_shift) ||
        !xx_olecf_yx_read32(data, size, 44, &num_fat) ||
        !xx_olecf_yx_read32(data, size, 48, &first_dir) ||
        !xx_olecf_yx_read32(data, size, 60, &first_mini_fat) ||
        !xx_olecf_yx_read32(data, size, 64, &mini_fat_count) ||
        !xx_olecf_yx_read32(data, size, 68, &first_difat) ||
        !xx_olecf_yx_read32(data, size, 72, &difat_count) ||
        byte_order != 0xfffe ||
        (sector_shift != 9 && sector_shift != 12)) return false;
    model->mini_sector_size = mini_sector_shift >= 1 &&
                              mini_sector_shift <= 16 ?
                              (size_t)1 << mini_sector_shift : 64;
    model->sector_size = (size_t)1 << sector_shift;
    entries_per_sector = model->sector_size / 4;
    possible = (size - 76) / 4;
    if (possible > 109) possible = 109;
    for (size_t i = 0; i < possible; ++i) {
        uint32_t sector;
        if (!xx_olecf_yx_read32(data, size, 76 + i * 4, &sector))
            return false;
        if (sector < XX_OLECF_YX_MAX_SECTOR &&
            !xx_olecf_yx_push_u32(&model->fat_sectors, &model->fat_count,
                                 &model->fat_capacity, sector)) return false;
    }
    max_fat_sectors = size / model->sector_size / entries_per_sector + 2;
    next_difat = first_difat;
    for (uint32_t i = 0; i < difat_count; ++i) {
        const uint8_t *sector;
        if (next_difat >= XX_OLECF_YX_MAX_SECTOR ||
            xx_olecf_yx_has_u32(model->visited_difat,
                              model->visited_difat_count, next_difat) ||
            !xx_olecf_yx_push_u32(&model->visited_difat,
                                 &model->visited_difat_count,
                                 &model->visited_difat_capacity,
                                 next_difat) ||
            !xx_olecf_yx_sector(model, next_difat, &sector)) break;
        for (size_t j = 0; j + 1 < entries_per_sector; ++j) {
            uint32_t fat_sector;
            if (model->fat_count >= max_fat_sectors) break;
            if (!xx_olecf_yx_read32(sector, model->sector_size,
                                   j * 4, &fat_sector)) break;
            if (fat_sector < XX_OLECF_YX_MAX_SECTOR &&
                !xx_olecf_yx_push_u32(&model->fat_sectors,
                                     &model->fat_count,
                                     &model->fat_capacity,
                                     fat_sector)) return false;
        }
        if (model->fat_count >= max_fat_sectors) break;
        if (!xx_olecf_yx_read32(sector, model->sector_size,
                               (entries_per_sector - 1) * 4,
                               &next_difat)) break;
    }
    if (first_dir >= XX_OLECF_YX_MAX_SECTOR ||
        (model->fat_count == 0 && num_fat > 0)) return false;

    {
        uint32_t current = first_dir;
        while (current < XX_OLECF_YX_MAX_SECTOR &&
               !xx_olecf_yx_has_u32(model->directory_sectors,
                                   model->directory_count, current)) {
            uint32_t next;
            if (!xx_olecf_yx_push_u32(&model->directory_sectors,
                                     &model->directory_count,
                                     &model->directory_capacity,
                                     current)) return false;
            if (!xx_olecf_yx_fat_entry(model, current, &next) ||
                next >= XX_OLECF_YX_MAX_SECTOR) break;
            current = next;
        }
    }
    if (!model->directory_count) return false;
    if (mini_fat_count > 0 && first_mini_fat < XX_OLECF_YX_MAX_SECTOR &&
        !xx_olecf_yx_follow_chain(model, first_mini_fat,
                               &model->mini_fat_sectors,
                               &model->mini_fat_count,
                               &model->mini_fat_capacity)) return false;

    for (size_t i = 0; i < model->directory_count; ++i) {
        uint64_t sector_offset =
            ((uint64_t)model->directory_sectors[i] + 1u) *
            model->sector_size;
        if (sector_offset > size) continue;
        for (size_t relative = 0;
             relative + 128 <= model->sector_size; relative += 128) {
            size_t at;
            if (sector_offset + relative > size ||
                size - (size_t)(sector_offset + relative) < 128) break;
            at = (size_t)(sector_offset + relative);
            if (!xx_olecf_yx_parse_entry(model, data + at)) return false;
        }
    }
    return true;
}

static void xx_olecf_yx_free(xx_olecf_yx_model *model)
{
    free(model->fat_sectors);
    free(model->mini_fat_sectors);
    free(model->visited_difat);
    free(model->directory_sectors);
    free(model->streams);
}

bool xx_olecf_yx_constant(const char *member, uint64_t *value)
{
    static const struct { const char *name; uint64_t value; } constants[] = {
        {"StreamType.UNKNOWN", 0},
        {"StreamType.STORAGE", 1},
        {"StreamType.STREAM", 2},
        {"StreamType.ROOT", 5}
    };
    if (!member || !value) return false;
    for (size_t i = 0; i < sizeof(constants) / sizeof(*constants); ++i) {
        if (strcmp(member, constants[i].name) == 0) {
            *value = constants[i].value;
            return true;
        }
    }
    return false;
}

bool xx_olecf_yx_is_olecf(const uint8_t *data, size_t size)
{
    xx_olecf_yx_model model;
    bool valid = xx_olecf_yx_parse(data, size, &model);
    xx_olecf_yx_free(&model);
    return valid;
}

bool xx_olecf_yx_stream_count(const uint8_t *data, size_t size,
                            uint64_t *count)
{
    xx_olecf_yx_model model;
    bool valid;
    if (!count || (!data && size)) return false;
    valid = xx_olecf_yx_parse(data, size, &model);
    *count = valid ? model.stream_count : 0;
    xx_olecf_yx_free(&model);
    return true;
}

bool xx_olecf_yx_stream_get(const uint8_t *data, size_t size,
                          uint64_t index, xx_olecf_yx_stream *stream)
{
    xx_olecf_yx_model model;
    bool valid;
    if (!stream || (!data && size)) return false;
    valid = xx_olecf_yx_parse(data, size, &model);
    if (valid && index < model.stream_count)
        *stream = model.streams[index];
    else valid = false;
    xx_olecf_yx_free(&model);
    return valid;
}

struct xx_olecf_yx_document {
    xx_olecf_yx_model model;
};

xx_olecf_yx_document *xx_olecf_yx_open(const uint8_t *data, size_t size)
{
    xx_olecf_yx_document *document;
    if (!data && size) return NULL;
    document = (xx_olecf_yx_document *)malloc(sizeof(*document));
    if (!document) return NULL;
    if (!xx_olecf_yx_parse(data, size, &document->model)) {
        xx_olecf_yx_free(&document->model);
        free(document);
        return NULL;
    }
    return document;
}

void xx_olecf_yx_close(xx_olecf_yx_document *document)
{
    if (!document) return;
    xx_olecf_yx_free(&document->model);
    free(document);
}

size_t xx_olecf_yx_sector_size(const xx_olecf_yx_document *document)
{
    return document ? document->model.sector_size : 0;
}

size_t xx_olecf_yx_entry_count(const xx_olecf_yx_document *document)
{
    return document ? document->model.stream_count : 0;
}

const xx_olecf_yx_stream *xx_olecf_yx_entry(const xx_olecf_yx_document *document,
                                      size_t index)
{
    if (!document || index >= document->model.stream_count) return NULL;
    return &document->model.streams[index];
}

/* Olecf::get_minifat_entry: a missing MiniFAT reads as ENDOFCHAIN. */
static bool xx_olecf_yx_minifat_entry(const xx_olecf_yx_model *model,
                                    uint32_t sector, uint32_t *next)
{
    size_t entries_per_sector = model->sector_size / 4;
    size_t fat_index = sector / entries_per_sector;
    const uint8_t *fat;
    if (fat_index >= model->mini_fat_count) {
        *next = XX_OLECF_YX_ENDOFCHAIN;
        return true;
    }
    if (!xx_olecf_yx_sector(model, model->mini_fat_sectors[fat_index], &fat))
        return false;
    return xx_olecf_yx_read32(fat, model->sector_size,
                            (sector % entries_per_sector) * 4, next);
}

static bool xx_olecf_yx_next_sector(const xx_olecf_yx_model *model, bool mini,
                                  uint32_t sector, uint32_t *next)
{
    return mini ? xx_olecf_yx_minifat_entry(model, sector, next) :
                  xx_olecf_yx_fat_entry(model, sector, next);
}

/* Olecf::get_stream_data_by_chain over `source` (the scanned data, or the
 * root mini stream). A strictly sequential chain whose sectors all lie in a
 * borrowed `source` is returned as a slice of it; otherwise the chain is
 * gathered sector by sector into a buffer returned in *owner. */
static bool xx_olecf_yx_chain_data(const xx_olecf_yx_model *model, bool mini,
                                 const uint8_t *source, size_t source_size,
                                 bool borrowed, uint64_t base_offset,
                                 size_t sector_size, uint32_t start_sector,
                                 size_t stream_size, const uint8_t **bytes,
                                 uint8_t **owner)
{
    static const uint8_t empty[1] = {0};
    uint32_t current = start_sector;
    uint32_t *visited = NULL;
    size_t visited_count = 0, visited_capacity = 0, used = 0;
    uint8_t *gathered;
    *bytes = NULL;
    *owner = NULL;
    if (stream_size == 0) {
        *bytes = empty;
        return true;
    }
    if (borrowed && start_sector < XX_OLECF_YX_MAX_SECTOR) {
        /* try_get_stream_slice */
        size_t needed = stream_size / sector_size +
                        (stream_size % sector_size != 0);
        bool sequential = true;
        uint64_t start_offset, span;
        for (size_t i = 1; i < needed; ++i) {
            uint32_t next;
            if (!xx_olecf_yx_next_sector(model, mini, current, &next) ||
                next != current + 1 || next >= XX_OLECF_YX_MAX_SECTOR) {
                sequential = false;
                break;
            }
            current = next;
        }
        start_offset = base_offset + (uint64_t)start_sector * sector_size;
        span = (uint64_t)needed * sector_size;
        if (sequential && start_offset <= source_size &&
            span <= (uint64_t)source_size - start_offset) {
            *bytes = source + (size_t)start_offset;
            return true;
        }
        current = start_sector;
    }
    gathered = (uint8_t *)malloc(stream_size);
    if (!gathered) return false;
    while (current < XX_OLECF_YX_MAX_SECTOR && used < stream_size) {
        uint64_t offset = base_offset + (uint64_t)current * sector_size;
        size_t take = stream_size - used < sector_size ?
                      stream_size - used : sector_size;
        if (xx_olecf_yx_has_u32(visited, visited_count, current) ||
            !xx_olecf_yx_push_u32(&visited, &visited_count, &visited_capacity,
                               current) ||
            offset >= source_size ||
            take > source_size - (size_t)offset) break;
        memcpy(gathered + used, source + (size_t)offset, take);
        used += take;
        if (used < stream_size) {
            uint32_t next;
            if (!xx_olecf_yx_next_sector(model, mini, current, &next)) break;
            if (next == XX_OLECF_YX_ENDOFCHAIN || next >= XX_OLECF_YX_MAX_SECTOR)
                break;
            current = next;
        }
    }
    free(visited);
    if (used != stream_size) {
        free(gathered);
        return false;
    }
    *bytes = gathered;
    *owner = gathered;
    return true;
}

bool xx_olecf_yx_entry_data(const xx_olecf_yx_document *document, size_t index,
                         const uint8_t **bytes, size_t *size,
                         uint8_t **owner)
{
    const xx_olecf_yx_model *model;
    const xx_olecf_yx_stream *entry;
    const uint8_t *root;
    uint8_t *root_owner;
    bool ok;
    if (!bytes || !size || !owner) return false;
    *bytes = NULL;
    *size = 0;
    *owner = NULL;
    entry = xx_olecf_yx_entry(document, index);
    if (!entry) return false;
    model = &document->model;
    if (entry->size > XX_OLECF_YX_MAX_STREAM_SIZE) return false;
    if (!(entry->size < 4096 && entry->type != 5)) {
        /* get_regular_stream_data */
        if (!xx_olecf_yx_chain_data(model, false, model->data, model->size, true,
                                 model->sector_size, model->sector_size,
                                 entry->start_sector, (size_t)entry->size,
                                 bytes, owner)) return false;
        *size = (size_t)entry->size;
        return true;
    }
    /* get_mini_stream_data: the root mini stream, then the MiniFAT chain
     * relative to it. A slice of a gathered root stream is not kept: like
     * the reference, a mini stream is then gathered into its own buffer. */
    if (model->mini_stream_size == 0 ||
        model->mini_stream_size > XX_OLECF_YX_MAX_STREAM_SIZE) return false;
    if (!xx_olecf_yx_chain_data(model, false, model->data, model->size, true,
                             model->sector_size, model->sector_size,
                             model->mini_stream_start,
                             (size_t)model->mini_stream_size, &root,
                             &root_owner)) return false;
    ok = xx_olecf_yx_chain_data(model, true, root,
                             (size_t)model->mini_stream_size,
                             root_owner == NULL, 0, model->mini_sector_size,
                             entry->start_sector, (size_t)entry->size,
                             bytes, owner);
    free(root_owner);
    if (ok) *size = (size_t)entry->size;
    return ok;
}
