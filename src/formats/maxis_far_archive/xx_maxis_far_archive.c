/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Maxis FAR archive, version 1 (variants 1a and 1b).  xx_maxis_far_archive.h
 * carries the field table.  Written from the format's structure; checked
 * against the GAUP PRO Total Commander plugin (which reads variant 1a).
 *
 * The whole manifest is walked before anything is published: every entry
 * must lie inside the manifest, every name must be 1..FAR_MAX_NAME bytes
 * without NUL.  An entry whose two size fields disagree (version 1 never
 * compresses) or whose data leaves the archive is listed but not extracted;
 * at least one entry must be intact.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/maxis_far_archive/xx_maxis_far_archive.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/var/xx_var.h"

#include <limits.h>
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

/* Registration placeholder: picks up the real file type as soon as the
 * enumerator exists in xxfc_defs.h. */
#ifdef MAXIS_FAR_ARCHIVE
#define XX_MAXIS_FAR_ARCHIVE_FILE_TYPE XX_FILE_TYPE_MAXIS_FAR_ARCHIVE
#else
#define XX_MAXIS_FAR_ARCHIVE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define FAR_HEADER_SIZE 16U
#define FAR_VERSION 1U
/* Fixed part of an entry before the name length field. */
#define FAR_ENTRY_FIXED 12U
/* The manifest is read into memory in one piece; a larger one is refused. */
#define FAR_MAX_MANIFEST (32U * 1024U * 1024U)
#define FAR_MAX_MEMBERS (1U << 20)
#define FAR_MAX_NAME 1024U
#define FAR_POLL_MASK 0x3FFU

typedef struct far_member_s {
    uint32_t size;
    uint32_t offset;       /**< From the start of the archive. */
    uint32_t entry_offset; /**< Manifest entry, from the archive start. */
    uint32_t entry_size;
    uint32_t name_at; /**< UTF-8 name in the pool. */
    uint32_t out_at;  /**< Output name in the out pool; 0 = unsafe. */
    uint8_t renamed;
    uint8_t blocked;
    uint8_t bad; /**< Sizes disagree or data out of range. */
} far_member;

typedef struct far_walk_s {
    far_member *items;
    size_t count;
    char *pool; /**< Original names, '/' separated, UTF-8. */
    size_t pool_length;
    char *out; /**< Output names (+1: offset 0 is ""). */
    size_t out_length;
    uint32_t manifest_offset;
    uint32_t width;
    int64_t extent; /**< Archive size in bytes. */
} far_walk;

typedef struct far_stream_s {
    far_walk walk;
    size_t index;
} far_stream;

static uint32_t far_le16(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U);
}

static bool far_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    const size_t io_capacity = xx_get_file_buffer_size();
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        size_t request = size - done;
        ssize_t amount;
        if (request > io_capacity) request = io_capacity;
        amount = xx_io_read(device, (uint8_t *)buffer + done, request);
        if (amount <= 0 || (size_t)amount > request) return false;
        done += (size_t)amount;
    }
    return true;
}

static void far_walk_free(far_walk *walk)
{
    if (walk->items) xx_mem_free(walk->items);
    if (walk->pool) xx_mem_free(walk->pool);
    if (walk->out) xx_mem_free(walk->out);
    walk->items = NULL;
    walk->pool = NULL;
    walk->out = NULL;
}

/* ---- manifest ----------------------------------------------------------- */

/* Walk all entries with name lengths of `width` bytes.  Validation only
 * when `walk` is NULL; otherwise the members and names are collected.
 * `names_size` receives the pool size the names need (UTF-8 can double
 * Latin-1 bytes). */
static bool far_walk_entries(const uint8_t *manifest, size_t available, uint32_t count, uint32_t width, int64_t total, uint32_t manifest_offset, far_walk *walk,
                             size_t *names_size, int64_t *extent, xx_pd_struct *pd)
{
    size_t position = 4U, pool = 0U;
    int64_t end = (int64_t)manifest_offset;
    uint32_t index, good = 0U;
    for (index = 0U; index < count; ++index) {
        uint32_t size, stored, offset, name_length, k;
        bool bad;
        const uint8_t *name;
        if ((index & FAR_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (available - position < FAR_ENTRY_FIXED + width) return false;
        size = xx_data_get_u32(manifest + position, 4, 0, false);
        stored = xx_data_get_u32(manifest + position + 4U, 4, 0, false);
        offset = xx_data_get_u32(manifest + position + 8U, 4, 0, false);
        name_length = width == 4U ? xx_data_get_u32(manifest + position + 12U, 4, 0, false) : far_le16(manifest + position + 12U);
        if (name_length == 0U || name_length > FAR_MAX_NAME || available - position - FAR_ENTRY_FIXED - width < name_length) return false;
        /* A damaged entry is still listed but never extracted: its sizes
         * disagree or its data leaves the archive. */
        bad = size != stored || (int64_t)offset > total || (int64_t)size > total - (int64_t)offset || (size != 0U && offset < FAR_HEADER_SIZE);
        if (!bad) ++good;
        name = manifest + position + FAR_ENTRY_FIXED + width;
        for (k = 0U; k < name_length; ++k)
            if (name[k] == 0U) return false;
        if (walk) {
            far_member *m = &walk->items[index];
            char *out = walk->pool + walk->pool_length;
            size_t length = 0U;
            m->size = size;
            m->offset = offset;
            m->bad = bad ? 1U : 0U;
            m->entry_offset = manifest_offset + (uint32_t)position;
            m->entry_size = FAR_ENTRY_FIXED + width + name_length;
            m->name_at = (uint32_t)walk->pool_length;
            for (k = 0U; k < name_length; ++k) {
                uint8_t c = name[k];
                if (c == '\\') c = '/';
                if (c < 0x80U) {
                    out[length++] = (char)c;
                } else { /* Latin-1 to UTF-8 */
                    out[length++] = (char)(0xC0U | (c >> 6U));
                    out[length++] = (char)(0x80U | (c & 0x3FU));
                }
            }
            out[length] = 0;
            walk->pool_length += length + 1U;
        }
        pool += (size_t)name_length * 2U + 1U;
        if (!bad && (int64_t)offset + (int64_t)size > end) end = (int64_t)offset + (int64_t)size;
        position += FAR_ENTRY_FIXED + width + name_length;
    }
    /* Some member must be intact, or this is not a FAR manifest. */
    if (count != 0U && good == 0U) return false;
    if ((int64_t)manifest_offset + (int64_t)position > end) end = (int64_t)manifest_offset + (int64_t)position;
    if (names_size) *names_size = pool;
    if (extent) *extent = end;
    return true;
}

/* ---- output names ------------------------------------------------------- */

static char far_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static char far_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool far_stem_is(const char *s, size_t stem, const char *word)
{
    size_t i;
    for (i = 0U; i < stem; ++i)
        if (!word[i] || far_upper(s[i]) != word[i]) return false;
    return word[stem] == 0;
}

/* One path component [s, s + length): refuse empty, dot-only, reserved
 * punctuation, control bytes and Windows device names in any case, with
 * or without an extension. */
static bool far_safe_component(const char *s, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t i, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (i = 0U; i < length; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && s[stem] != '.') ++stem;
    while (stem > 0U && s[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (far_stem_is(s, stem, devices[i])) return false;
    if (stem == 4U && s[3] >= '0' && s[3] <= '9' &&
        ((far_upper(s[0]) == 'C' && far_upper(s[1]) == 'O' && far_upper(s[2]) == 'M') || (far_upper(s[0]) == 'L' && far_upper(s[1]) == 'P' && far_upper(s[2]) == 'T')))
        return false;
    return true;
}

/* A relative path of safe components; no leading, trailing or doubled
 * separator, so absolute paths and drive letters are refused too. */
static bool far_safe_path(const char *name)
{
    size_t start = 0U, i = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        if (name[i] == '/' || name[i] == 0) {
            if (!far_safe_component(name + start, i - start)) return false;
            if (name[i] == 0) return true;
            start = i + 1U;
        }
        ++i;
    }
}

static uint32_t far_hash(const char *s)
{
    uint32_t h = 2166136261U;
    for (; *s; ++s) h = (h ^ (uint8_t)far_lower(*s)) * 16777619U;
    return h;
}

static bool far_same(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
        if (far_lower(*a) != far_lower(*b)) return false;
    return *a == *b;
}

typedef struct far_key_s {
    uint32_t hash;
    uint32_t index;
} far_key;

static int far_compare_keys(const void *left, const void *right)
{
    const far_key *a = (const far_key *)left;
    const far_key *b = (const far_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* Mark every member whose output name repeats (case-insensitively) the
 * output name of an earlier member.  `rename` sets `renamed`, otherwise
 * `blocked`. */
static bool far_find_repeats(far_walk *walk, bool rename)
{
    far_key *keys;
    size_t i, j, n = 0U;
    if (walk->count < 2U) return true;
    keys = (far_key *)xx_mem_alloc(walk->count * sizeof(*keys));
    if (!keys) return false;
    for (i = 0U; i < walk->count; ++i) {
        if (walk->items[i].out_at == 0U) continue;
        keys[n].hash = far_hash(walk->out + walk->items[i].out_at);
        keys[n].index = (uint32_t)i;
        ++n;
    }
    xx_rt_qsort(keys, n, sizeof(*keys), far_compare_keys);
    for (i = 1U; i < n; ++i) {
        far_member *m = &walk->items[keys[i].index];
        for (j = i; j > 0U && keys[j - 1U].hash == keys[i].hash; --j) {
            const far_member *e = &walk->items[keys[j - 1U].index];
            if (far_same(walk->out + e->out_at, walk->out + m->out_at)) {
                if (rename) m->renamed = 1U;
                else m->blocked = 1U;
                break;
            }
        }
    }
    xx_mem_free(keys);
    return true;
}

static size_t far_decimal(uint32_t value, char *text)
{
    char digits[12];
    size_t n = 0U, i;
    do {
        digits[n++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    for (i = 0U; i < n; ++i) text[i] = digits[n - 1U - i];
    return n;
}

/* Build the output name of every member: its own path when safe, with
 * "_<index>" before the extension of the last component when an earlier
 * member already claims that name.  A name that still collides after the
 * rename is blocked rather than allowed to overwrite. */
static bool far_build_output_names(far_walk *walk)
{
    size_t i, capacity = walk->pool_length + walk->count * 12U + 1U;
    char *out;
    size_t length = 1U;
    out = (char *)xx_mem_alloc(capacity);
    if (!out) return false;
    out[0] = 0;
    for (i = 0U; i < walk->count; ++i) {
        far_member *m = &walk->items[i];
        const char *name = walk->pool + m->name_at;
        size_t n = xx_str_len(name);
        m->out_at = 0U;
        if (!far_safe_path(name)) continue;
        m->out_at = (uint32_t)length;
        xx_rt_memcpy(out + length, name, n + 1U);
        length += n + 1U;
    }
    walk->out = out;
    walk->out_length = length;
    if (!far_find_repeats(walk, true)) return false;
    for (i = 0U; i < walk->count; ++i) {
        far_member *m = &walk->items[i];
        const char *name;
        size_t n, dot, slash, k;
        if (!m->renamed) continue;
        name = walk->out + m->out_at;
        n = xx_str_len(name);
        slash = 0U;
        for (k = 0U; k < n; ++k)
            if (name[k] == '/') slash = k + 1U;
        dot = n;
        for (k = n; k > slash; --k)
            if (name[k - 1U] == '.') {
                dot = k - 1U;
                break;
            }
        if (dot == slash) dot = n; /* ".hidden": suffix at the end */
        if (length + n + 13U > capacity) return false;
        {
            char *at = out + length;
            size_t w = 0U;
            xx_rt_memcpy(at, name, dot);
            w = dot;
            at[w++] = '_';
            w += far_decimal((uint32_t)i, at + w);
            xx_rt_memcpy(at + w, name + dot, n - dot);
            w += n - dot;
            at[w] = 0;
            m->out_at = (uint32_t)length;
            length += w + 1U;
        }
    }
    walk->out_length = length;
    return far_find_repeats(walk, false);
}

/* ---- parse -------------------------------------------------------------- */

/* Parse the archive.  With `walk` NULL only validity is established. */
static bool far_parse(Abstractformat *format, far_walk *walk, uint32_t *manifest_offset_out, uint32_t *width_out, uint32_t *count_out, int64_t *extent_out,
                      xx_pd_struct *pd)
{
    uint8_t header[FAR_HEADER_SIZE];
    uint8_t *manifest = NULL;
    int64_t total, extent = 0;
    uint32_t manifest_offset, count, width = 0U;
    size_t available, names_size = 0U;
    bool ok = false;
    if (walk) xx_mem_zero(walk, sizeof(*walk));
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    total -= format->base_address;
    if (total < (int64_t)FAR_HEADER_SIZE + 4) return false;
    if (!far_read_at(format->device, format->base_address, header, sizeof(header)) || xx_rt_memcmp(header, "FAR!byAZ", 8U) != 0 ||
        xx_data_get_u32(header + 8U, 4, 0, false) != FAR_VERSION)
        return false;
    manifest_offset = xx_data_get_u32(header + 12U, 4, 0, false);
    if (manifest_offset < FAR_HEADER_SIZE || (int64_t)manifest_offset > total - 4) return false;
    available = total - (int64_t)manifest_offset > (int64_t)FAR_MAX_MANIFEST ? FAR_MAX_MANIFEST : (size_t)(total - (int64_t)manifest_offset);
    {
        uint8_t first[4];
        if (!far_read_at(format->device, format->base_address + (int64_t)manifest_offset, first, 4U)) return false;
        count = xx_data_get_u32(first, 4, 0, false);
    }
    /* Every entry takes at least 12 + 2 + 1 bytes. */
    if (count > FAR_MAX_MEMBERS || (uint64_t)count * (FAR_ENTRY_FIXED + 3U) > available - 4U) return false;
    manifest = (uint8_t *)xx_mem_alloc(available);
    if (!manifest) return false;
    if (!far_read_at(format->device, format->base_address + (int64_t)manifest_offset, manifest, available)) goto done;
    if (far_walk_entries(manifest, available, count, 4U, total, manifest_offset, NULL, &names_size, &extent, pd)) width = 4U;
    else if (far_walk_entries(manifest, available, count, 2U, total, manifest_offset, NULL, &names_size, &extent, pd)) width = 2U;
    else goto done;
    if (walk) {
        walk->items = (far_member *)xx_mem_calloc(count ? count : 1U, sizeof(far_member));
        walk->pool = (char *)xx_mem_alloc(names_size + 1U);
        if (!walk->items || !walk->pool) goto done;
        walk->count = count;
        if (!far_walk_entries(manifest, available, count, width, total, manifest_offset, walk, NULL, NULL, pd) || !far_build_output_names(walk)) goto done;
        walk->manifest_offset = manifest_offset;
        walk->width = width;
        walk->extent = extent;
    }
    if (manifest_offset_out) *manifest_offset_out = manifest_offset;
    if (width_out) *width_out = width;
    if (count_out) *count_out = count;
    if (extent_out) *extent_out = extent;
    ok = true;
done:
    xx_mem_free(manifest);
    if (!ok && walk) far_walk_free(walk);
    return ok;
}

/* ---- records ------------------------------------------------------------ */

static void far_stream_free(void *opaque)
{
    far_stream *stream = (far_stream *)opaque;
    if (!stream) return;
    far_walk_free(&stream->walk);
    xx_mem_free(stream);
}

static bool far_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *far_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool far_set_record(xx_archive_record *record, const far_walk *walk, size_t index, int64_t base)
{
    const far_member *m = &walk->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base + (int64_t)m->entry_offset;
    record->header_size = (int64_t)m->entry_size;
    record->data_offset = base + (int64_t)m->offset;
    record->compressed_size = (int64_t)m->size;
    return xx_archive_record_set_original_name(record, walk->pool + m->name_at) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API --------------------------------------------------------- */

void xx_maxis_far_archive_init(xx_maxis_far_archive *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MAXIS_FAR_ARCHIVE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-maxis-far");
    xx_format_set_extension(&archive->format, "far");
    archive->format.check_is_valid = xx_maxis_far_archive_check_is_valid;
    archive->format.handle_base_info = xx_maxis_far_archive_handle_base_info;
    archive->format.get_format_size = xx_maxis_far_archive_get_format_size;
    archive->format.get_number_of_archive_records = xx_maxis_far_archive_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_maxis_far_archive_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_maxis_far_archive_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_maxis_far_archive_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_maxis_far_archive_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_maxis_far_archive_free_archive_records_reading;
}

xx_maxis_far_archive *xx_maxis_far_archive_create(xx_io_device *device, int64_t base_address)
{
    xx_maxis_far_archive *archive = (xx_maxis_far_archive *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_maxis_far_archive_init(archive, device, base_address);
    return archive;
}

void xx_maxis_far_archive_destroy(xx_maxis_far_archive *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_maxis_far_archive_free(xx_maxis_far_archive *archive)
{
    if (!archive) return;
    xx_maxis_far_archive_destroy(archive);
    xx_mem_free(archive);
}

bool xx_maxis_far_archive_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return far_parse(format, NULL, NULL, NULL, NULL, NULL, pd);
}

bool xx_maxis_far_archive_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    xx_maxis_far_archive *archive;
    uint32_t manifest_offset, width, count;
    int64_t extent;
    if (!format || !far_parse(format, NULL, &manifest_offset, &width, &count, &extent, pd)) return false;
    archive = (xx_maxis_far_archive *)format;
    archive->number_of_records = count;
    archive->manifest_offset = manifest_offset;
    archive->name_length_width = width;
    format->number_of_archive_records = count;
    format->format_size = extent;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_maxis_far_archive_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_maxis_far_archive_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_maxis_far_archive_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_maxis_far_archive_handle_base_info(format, pd)) ? ((xx_maxis_far_archive *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_maxis_far_archive_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    far_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (far_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!far_parse(format, &stream->walk, NULL, NULL, NULL, NULL, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        far_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = far_stream_free;
    state->total_records = (int64_t)stream->walk.count;
    if (!far_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->walk.count != 0U) {
        if (!far_set_record(&state->current_record, &stream->walk, 0U, format->base_address)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
    }
    return state;
}

const xx_archive_record *xx_maxis_far_archive_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_maxis_far_archive_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    far_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (far_stream *)state->internal_state)) return false;
    if (stream->index + 1U >= stream->walk.count) {
        stream->index = stream->walk.count;
        state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!far_set_record(&state->current_record, &stream->walk, stream->index, format->base_address)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_maxis_far_archive_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    far_stream *stream;
    const far_member *m;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    int64_t data;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (far_stream *)state->internal_state) || stream->index >= stream->walk.count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    m = &stream->walk.items[stream->index];
    data = format->base_address + (int64_t)m->offset;
    option = far_option(&state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)m->size > xx_var_get_u64(option)) return false;
    option = far_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (m->bad) return false;
    if (!option) /* No destination: verify the data is present. */
        return xx_io_total_size(format->device) >= data + (int64_t)m->size;
    if (m->out_at == 0U || m->blocked) return false;
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", stream->walk.out + m->out_at)
                                                                                                  : xx_str_concat(base, stream->walk.out + m->out_at);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    /* The store helper removes its own output on failure (and only when it
     * created it), so nothing is removed here. */
    result = xx_store_unpack_device_to_file(format->device, data, (int64_t)m->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_maxis_far_archive_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
