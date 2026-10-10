/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RPG Maker RGSSAD resource archive (Game.rgssad / .rgss2a / .rgss3a).
 * xx_rpg_maker_rgssad.h carries the field table, the key stream and the
 * member-name rules.  Written from the format's structure; the layout and
 * acceptance rules follow XArchive's games/xrgssad.cpp (MIT) and GARbro's
 * Experimental/RPGMaker/ArcRGSS.cs (MIT, morkt).  The member-name safety
 * and duplicate handling follow this library's nsa reader
 * (src/formats/nsa/xx_nsa.c, MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rpg_maker_rgssad/xx_rpg_maker_rgssad.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef RPG_MAKER_RGSSAD
#define XX_RPG_MAKER_RGSSAD_FILE_TYPE XX_FILE_TYPE_RPG_MAKER_RGSSAD
#else
#define XX_RPG_MAKER_RGSSAD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RGSS_HEADER_SIZE 8
#define RGSS_V3_TABLE 12
#define RGSS_V1_KEY UINT32_C(0xDEADCAFE)
#define RGSS_MAX_NAME 1024U
/* Real games hold a few thousand members; this bounds the bookkeeping
 * (about 12 MiB) whatever the file claims. */
#define RGSS_MAX_MEMBERS 262144U
#define RGSS_NAME_BUFFER (3U * RGSS_MAX_NAME + 2U + 12U + 1U)
#define RGSS_POLL_MASK 0x3ffU

typedef struct rgss_member_s {
    int64_t entry_offset; /**< Absolute offset of the entry. */
    int64_t name_offset;  /**< Absolute offset of the encrypted name. */
    int64_t data_offset;  /**< Absolute offset of the data. */
    uint32_t size;
    uint32_t data_key;
    uint32_t name_key; /**< Key in force at the first name byte. */
    uint16_t name_length;
    bool renamed;
} rgss_member;

typedef struct rgss_key_s {
    uint64_t hash;
    uint32_t index;
} rgss_key;

typedef struct rgss_layout_s {
    int64_t origin; /**< Absolute offset of the header. */
    int64_t size;   /**< From the header to EOF. */
    int64_t end;    /**< Format size: extent of header, table and data. */
    uint32_t version;
    uint32_t count;
} rgss_layout;

typedef struct rgss_stream_s {
    rgss_member *items;
    size_t count;
    size_t index;
    char *name;
} rgss_stream;

/* ---- helpers ----------------------------------------------------------- */

static uint32_t rgss_next(uint32_t key)
{
    return key * 7U + 3U;
}

static size_t rgss_capacity(void)
{
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (n < 4096U) n = 4096U;
    if (n > (SIZE_MAX >> 1)) n = SIZE_MAX >> 1;
    return n & ~(size_t)3U; /* whole key words per chunk */
}

static bool rgss_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
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

static bool rgss_write_all(xx_io_device *destination, const uint8_t *data, size_t size)
{
    size_t done = 0U;
    if (!destination) return true;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---- member names ------------------------------------------------------ */

static bool rgss_valid_utf8(const uint8_t *raw, size_t length)
{
    size_t index = 0U;
    while (index < length) {
        uint8_t c = raw[index];
        size_t extra, k;
        uint32_t cp;
        if (c < 0x80U) {
            ++index;
            continue;
        }
        if (c >= 0xc2U && c <= 0xdfU) {
            extra = 1U;
            cp = c & 0x1fU;
        } else if (c >= 0xe0U && c <= 0xefU) {
            extra = 2U;
            cp = c & 0x0fU;
        } else if (c >= 0xf0U && c <= 0xf4U) {
            extra = 3U;
            cp = c & 0x07U;
        } else {
            return false;
        }
        if (length - index - 1U < extra) return false;
        for (k = 1U; k <= extra; ++k) {
            uint8_t t = raw[index + k];
            if ((t & 0xc0U) != 0x80U) return false;
            cp = (cp << 6U) | (t & 0x3fU);
        }
        if ((extra == 2U && (cp < 0x800U || (cp >= 0xd800U && cp <= 0xdfffU))) || (extra == 3U && (cp < 0x10000U || cp > 0x10ffffU))) return false;
        index += extra + 1U;
    }
    return true;
}

static size_t rgss_put_escape(char *out, uint8_t c)
{
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '%';
    out[1] = digits[(c >> 4U) & 0x0fU];
    out[2] = digits[c & 0x0fU];
    return 3U;
}

/* `out` holds at least 3 * length + 1 bytes. */
static size_t rgss_convert_name(const uint8_t *raw, size_t length, char *out)
{
    size_t at = 0U, index;
    bool utf8 = rgss_valid_utf8(raw, length);
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c == (uint8_t)'\\') out[at++] = '/';
        else if (!utf8 && (c >= 0x80U || c == (uint8_t)'%')) at += rgss_put_escape(out + at, c);
        else out[at++] = (char)c;
    }
    out[at] = 0;
    return at;
}

static uint64_t rgss_name_hash(const char *name, size_t length)
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

static void rgss_insert_suffix(char *name, size_t length, uint32_t index)
{
    char suffix[2 + 10];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '%';
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= RGSS_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at) name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

static bool rgss_reserved_component(const char *segment, size_t length)
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

static bool rgss_safe_name(const char *name)
{
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*' || c == '\\' || c == 0x7fU || (c != 0U && c < 0x20U)) return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || rgss_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* Decrypt a raw name in place.  Version 1 advances `key` per byte; version
 * 3 cycles through the bytes of a fixed key.  False on a control byte. */
static bool rgss_decrypt_name(uint8_t *raw, size_t length, uint32_t version, uint32_t key)
{
    size_t index;
    for (index = 0U; index < length; ++index) {
        if (version == 1U) {
            raw[index] ^= (uint8_t)key;
            key = rgss_next(key);
        } else {
            raw[index] ^= (uint8_t)(key >> (8U * (unsigned)(index & 3U)));
        }
        if (raw[index] < 0x20U || raw[index] == 0x7fU) return false;
    }
    return true;
}

/* ---- index walk -------------------------------------------------------- */

static bool rgss_read_header(Abstractformat *format, rgss_layout *layout)
{
    uint8_t header[RGSS_HEADER_SIZE];
    int64_t total;
    if (!format || !format->device || !layout || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(layout, sizeof(*layout));
    layout->origin = format->base_address;
    layout->size = total - format->base_address;
    if (layout->size < RGSS_HEADER_SIZE || !rgss_read_at(format->device, layout->origin, header, sizeof(header)) || xx_rt_memcmp(header, "RGSSAD\0", 7U) != 0 ||
        (header[7] != 1U && header[7] != 3U))
        return false;
    layout->version = header[7];
    return true;
}

/* Record one member (pass 2) and hash its converted name. */
static void rgss_store(rgss_member *items, rgss_key *keys, char *name, uint32_t index, const uint8_t *raw, const rgss_member *member)
{
    size_t converted;
    if (!items) return;
    items[index] = *member;
    converted = rgss_convert_name(raw, member->name_length, name);
    keys[index].hash = rgss_name_hash(name, converted);
    keys[index].index = index;
}

static bool rgss_walk_v1(Abstractformat *format, rgss_layout *layout, rgss_member *items, rgss_key *keys, char *name, uint32_t limit, xx_pd_struct *pd)
{
    uint8_t raw[RGSS_MAX_NAME];
    uint8_t field[4];
    int64_t pos = RGSS_HEADER_SIZE;
    const int64_t end = layout->size;
    uint32_t key = RGSS_V1_KEY, count = 0U;
    while (pos < end) {
        rgss_member member;
        uint32_t length, size;
        if (count >= limit) return false;
        if ((count & RGSS_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (end - pos < 4 || !rgss_read_at(format->device, layout->origin + pos, field, 4U)) return false;
        length = xx_data_get_u32(field, 4, 0, false) ^ key;
        key = rgss_next(key);
        pos += 4;
        if (length == 0U || length > RGSS_MAX_NAME || end - pos < (int64_t)length + 4 || !rgss_read_at(format->device, layout->origin + pos, raw, length)) return false;
        xx_mem_zero(&member, sizeof(member));
        member.entry_offset = layout->origin + pos - 4;
        member.name_offset = layout->origin + pos;
        member.name_key = key;
        member.name_length = (uint16_t)length;
        if (!rgss_decrypt_name(raw, length, 1U, key)) return false;
        {
            uint32_t k;
            for (k = 0U; k < length; ++k) key = rgss_next(key);
        }
        pos += (int64_t)length;
        if (!rgss_read_at(format->device, layout->origin + pos, field, 4U)) return false;
        size = xx_data_get_u32(field, 4, 0, false) ^ key;
        key = rgss_next(key);
        pos += 4;
        if ((int64_t)size > end - pos) return false;
        member.data_offset = layout->origin + pos;
        member.size = size;
        member.data_key = key;
        rgss_store(items, keys, name, count, raw, &member);
        ++count;
        pos += (int64_t)size;
    }
    if (count == 0U) return false;
    layout->count = count;
    layout->end = pos;
    return true;
}

static bool rgss_walk_v3(Abstractformat *format, rgss_layout *layout, rgss_member *items, rgss_key *keys, char *name, uint32_t limit, xx_pd_struct *pd)
{
    uint8_t raw[RGSS_MAX_NAME];
    uint8_t fields[16];
    int64_t pos = RGSS_V3_TABLE, extent, lowest = INT64_MAX;
    const int64_t end = layout->size;
    uint32_t key, count = 0U;
    if (end < RGSS_V3_TABLE + 4 || !rgss_read_at(format->device, layout->origin + RGSS_HEADER_SIZE, fields, 4U)) return false;
    key = xx_data_get_u32(fields, 4, 0, false) * 9U + 3U;
    extent = RGSS_V3_TABLE;
    for (;;) {
        rgss_member member;
        uint32_t offset, size, length;
        if ((count & RGSS_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (end - pos < 4 || !rgss_read_at(format->device, layout->origin + pos, fields, 4U)) return false;
        offset = xx_data_get_u32(fields, 4, 0, false) ^ key;
        if (offset == 0U) {
            pos += 4;
            break;
        }
        if (count >= limit) return false;
        if (end - pos < 16 || !rgss_read_at(format->device, layout->origin + pos, fields, 16U)) return false;
        size = xx_data_get_u32(fields + 4, 4, 0, false) ^ key;
        length = xx_data_get_u32(fields + 12, 4, 0, false) ^ key;
        xx_mem_zero(&member, sizeof(member));
        member.entry_offset = layout->origin + pos;
        member.data_key = xx_data_get_u32(fields + 8, 4, 0, false) ^ key;
        pos += 16;
        if (length == 0U || length > RGSS_MAX_NAME || end - pos < (int64_t)length || !rgss_read_at(format->device, layout->origin + pos, raw, length) ||
            !rgss_decrypt_name(raw, length, 3U, key))
            return false;
        member.name_offset = layout->origin + pos;
        member.name_key = key;
        member.name_length = (uint16_t)length;
        pos += (int64_t)length;
        if ((int64_t)offset > end || (int64_t)size > end - (int64_t)offset) return false;
        member.data_offset = layout->origin + (int64_t)offset;
        member.size = size;
        if ((int64_t)offset < lowest) lowest = (int64_t)offset;
        if ((int64_t)offset + (int64_t)size > extent) extent = (int64_t)offset + (int64_t)size;
        rgss_store(items, keys, name, count, raw, &member);
        ++count;
    }
    /* Every member lies behind the table. */
    if (count == 0U || lowest < pos) return false;
    layout->count = count;
    layout->end = extent > pos ? extent : pos;
    return true;
}

static bool rgss_walk(Abstractformat *format, rgss_layout *layout, rgss_member *items, rgss_key *keys, char *name, uint32_t limit, xx_pd_struct *pd)
{
    return layout->version == 1U ? rgss_walk_v1(format, layout, items, keys, name, limit, pd) : rgss_walk_v3(format, layout, items, keys, name, limit, pd);
}

static bool rgss_parse(Abstractformat *format, rgss_layout *layout, xx_pd_struct *pd)
{
    return rgss_read_header(format, layout) && rgss_walk(format, layout, NULL, NULL, NULL, RGSS_MAX_MEMBERS, pd);
}

static int rgss_compare_keys(const void *left, const void *right)
{
    const rgss_key *a = (const rgss_key *)left;
    const rgss_key *b = (const rgss_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static void rgss_mark_duplicates(rgss_member *items, rgss_key *keys, size_t count)
{
    size_t index;
    if (count < 2U) return;
    xx_rt_qsort(keys, count, sizeof(*keys), rgss_compare_keys);
    for (index = 1U; index < count; ++index)
        if (keys[index].hash == keys[index - 1U].hash && keys[index].index < count) items[keys[index].index].renamed = true;
}

static void rgss_stream_free(void *opaque)
{
    rgss_stream *stream = (rgss_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static bool rgss_open_stream(Abstractformat *format, rgss_stream **result, xx_pd_struct *pd)
{
    rgss_layout layout, second;
    rgss_member *items = NULL;
    rgss_key *keys = NULL;
    char *name = NULL;
    rgss_stream *stream = NULL;
    if (!result || !rgss_parse(format, &layout, pd)) return false;
    items = (rgss_member *)xx_mem_calloc(layout.count, sizeof(*items));
    keys = (rgss_key *)xx_mem_alloc((size_t)layout.count * sizeof(*keys));
    name = (char *)xx_mem_alloc(RGSS_NAME_BUFFER);
    stream = (rgss_stream *)xx_mem_calloc(1U, sizeof(*stream));
    second = layout;
    /* The second pass fills exactly the entries the first one counted. */
    if (!items || !keys || !name || !stream || !rgss_walk(format, &second, items, keys, name, layout.count, pd) || second.count != layout.count) goto fail;
    rgss_mark_duplicates(items, keys, layout.count);
    xx_mem_free(keys);
    stream->items = items;
    stream->count = layout.count;
    stream->name = name;
    *result = stream;
    return true;
fail:
    if (items) xx_mem_free(items);
    if (keys) xx_mem_free(keys);
    if (name) xx_mem_free(name);
    if (stream) xx_mem_free(stream);
    return false;
}

static bool rgss_load_name(Abstractformat *format, rgss_stream *stream, size_t index, uint32_t version)
{
    const rgss_member *member = &stream->items[index];
    uint8_t raw[RGSS_MAX_NAME];
    size_t length;
    if (member->name_length == 0U || member->name_length > RGSS_MAX_NAME || !rgss_read_at(format->device, member->name_offset, raw, member->name_length) ||
        !rgss_decrypt_name(raw, member->name_length, version, member->name_key))
        return false;
    length = rgss_convert_name(raw, member->name_length, stream->name);
    if (member->renamed) rgss_insert_suffix(stream->name, length, (uint32_t)index);
    return true;
}

/* ---- data -------------------------------------------------------------- */

static bool rgss_unpack_member(xx_io_device *source, const rgss_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    const size_t capacity = rgss_capacity();
    uint8_t *buffer;
    int64_t done = 0;
    const int64_t size = (int64_t)member->size;
    uint32_t key = member->data_key;
    bool ok = true;
    if (size == 0) return true;
    buffer = (uint8_t *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    while (done < size) {
        size_t chunk = size - done > (int64_t)capacity ? capacity : (size_t)(size - done);
        size_t at;
        if ((pd && xx_pd_is_stopped(pd)) || !rgss_read_at(source, member->data_offset + done, buffer, chunk)) {
            ok = false;
            break;
        }
        /* Chunks are whole key words except the last, so the key stays
         * aligned with the member's byte positions. */
        for (at = 0U; at < chunk; ++at) {
            buffer[at] ^= (uint8_t)(key >> (8U * (unsigned)(at & 3U)));
            if ((at & 3U) == 3U) key = rgss_next(key);
        }
        if (!rgss_write_all(destination, buffer, chunk)) {
            ok = false;
            break;
        }
        done += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return ok;
}

/* ---- records ----------------------------------------------------------- */

static bool rgss_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *rgss_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static uint32_t rgss_version_of(Abstractformat *format)
{
    return ((xx_rpg_maker_rgssad *)format)->rgss_version;
}

static bool rgss_set_record(Abstractformat *format, xx_archive_record *record, rgss_stream *stream, size_t index)
{
    const rgss_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!rgss_load_name(format, stream, index, rgss_version_of(format))) return false;
    record->header_offset = member->entry_offset;
    record->header_size = member->name_offset + member->name_length - member->entry_offset;
    if (rgss_version_of(format) == 1U) record->header_size += 4;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, stream->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_rpg_maker_rgssad_init(xx_rpg_maker_rgssad *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_RPG_MAKER_RGSSAD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "rgssad");
    archive->format.check_is_valid = xx_rpg_maker_rgssad_check_is_valid;
    archive->format.handle_base_info = xx_rpg_maker_rgssad_handle_base_info;
    archive->format.get_format_size = xx_rpg_maker_rgssad_get_format_size;
    archive->format.get_number_of_archive_records = xx_rpg_maker_rgssad_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_rpg_maker_rgssad_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_rpg_maker_rgssad_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_rpg_maker_rgssad_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_rpg_maker_rgssad_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_rpg_maker_rgssad_free_archive_records_reading;
}

xx_rpg_maker_rgssad *xx_rpg_maker_rgssad_create(xx_io_device *device, int64_t base_address)
{
    xx_rpg_maker_rgssad *archive = (xx_rpg_maker_rgssad *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rpg_maker_rgssad_init(archive, device, base_address);
    return archive;
}

void xx_rpg_maker_rgssad_destroy(xx_rpg_maker_rgssad *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rpg_maker_rgssad_free(xx_rpg_maker_rgssad *archive)
{
    if (!archive) return;
    xx_rpg_maker_rgssad_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rpg_maker_rgssad_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    rgss_layout layout;
    return rgss_parse(format, &layout, pd);
}

bool xx_rpg_maker_rgssad_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    rgss_layout layout;
    xx_rpg_maker_rgssad *archive;
    if (!rgss_parse(format, &layout, pd)) return false;
    archive = (xx_rpg_maker_rgssad *)format;
    archive->number_of_records = layout.count;
    archive->rgss_version = layout.version;
    format->version[0] = (char)('0' + (char)layout.version);
    format->version[1] = 0;
    xx_format_set_extension(format, layout.version == 3U ? "rgss3a" : "rgssad");
    format->number_of_archive_records = layout.count;
    format->format_size = layout.end;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_rpg_maker_rgssad_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_rpg_maker_rgssad_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_rpg_maker_rgssad_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_rpg_maker_rgssad_handle_base_info(format, pd)) ? ((xx_rpg_maker_rgssad *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_rpg_maker_rgssad_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    rgss_stream *stream;
    xx_archive_record_state *state;
    if (!format || (!format->base_info_handled && !xx_rpg_maker_rgssad_handle_base_info(format, pd))) return NULL;
    if (!rgss_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rgss_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rgss_stream_free;
    state->total_records = stream->count;
    if (!rgss_copy_options(&state->options, options) || !rgss_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_rpg_maker_rgssad_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_rpg_maker_rgssad_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    rgss_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (rgss_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = rgss_set_record(format, &state->current_record, stream, stream->index);
    return state->has_record;
}

bool xx_rpg_maker_rgssad_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    rgss_stream *stream;
    const rgss_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (rgss_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = rgss_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) /* No destination: decrypt into nothing, which verifies the reads. */
        return rgss_unpack_member(format->device, member, NULL, pd);
    if (!rgss_safe_name(stream->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->name)
                                                                                                  : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = rgss_unpack_member(format->device, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rpg_maker_rgssad_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
