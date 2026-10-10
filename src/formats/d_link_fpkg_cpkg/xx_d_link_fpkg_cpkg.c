/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * D-Link FPKG / CPKG firmware package.  xx_d_link_fpkg_cpkg.h carries the
 * field table.  The layout and the acceptance rules (header length 28, type
 * 0x100..0x103, ASCII name) come from unblob's
 * python/unblob/handlers/archive/dlink/fpkg.py (unblob 26.6.4, MIT, ONEKEY);
 * the code here is written from that description, not translated from it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/d_link_fpkg_cpkg/xx_d_link_fpkg_cpkg.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef D_LINK_FPKG_CPKG
#define XX_D_LINK_FPKG_CPKG_FILE_TYPE XX_FILE_TYPE_D_LINK_FPKG_CPKG
#else
#define XX_D_LINK_FPKG_CPKG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define FPKG_NAME_BUFFER 40U
#define FPKG_POLL_MASK 0x3ffU

typedef struct fpkg_member_s {
    int64_t header_offset; /**< Absolute offset of the 28-byte entry header. */
    int64_t data_offset;
    uint32_t size;
    uint16_t type;
    uint16_t unknown;
    char name[FPKG_NAME_BUFFER];
} fpkg_member;

typedef struct fpkg_layout_s {
    int64_t total; /**< Device size. */
    int64_t first; /**< Absolute offset of the first entry. */
    int64_t end;   /**< Absolute end of the last good entry. */
    uint32_t first_entry_offset;
    uint32_t count;
    bool is_fpkg;
} fpkg_layout;

typedef struct fpkg_stream_s {
    fpkg_member *items;
    size_t count;
    size_t index;
} fpkg_stream;

/* ---- I/O --------------------------------------------------------------- */

static bool fpkg_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
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

/* ---- header and entry walk --------------------------------------------- */

static bool fpkg_is_magic(const uint8_t *header)
{
    return (header[0] == (uint8_t)'F' || header[0] == (uint8_t)'C') && header[1] == (uint8_t)'P' && header[2] == (uint8_t)'K' && header[3] == (uint8_t)'G' &&
           header[4] == 0x01U && header[5] == 0U && header[6] == 0U && header[7] == 0U;
}

static bool fpkg_read_header(Abstractformat *format, fpkg_layout *layout)
{
    uint8_t header[XX_D_LINK_FPKG_CPKG_HEADER_SIZE];
    int64_t total;
    uint32_t first;
    if (!format || !format->device || !layout || format->base_address < 0) return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address || total - format->base_address < (int64_t)(XX_D_LINK_FPKG_CPKG_HEADER_SIZE + XX_D_LINK_FPKG_CPKG_ENTRY_SIZE) ||
        !fpkg_read_at(format->device, format->base_address, header, sizeof(header)) || !fpkg_is_magic(header))
        return false;
    first = xx_data_get_u32(header + 8U, 4, 0, true);
    if (first < XX_D_LINK_FPKG_CPKG_HEADER_SIZE || (int64_t)first > total - format->base_address - (int64_t)XX_D_LINK_FPKG_CPKG_ENTRY_SIZE) return false;
    layout->total = total;
    layout->first = format->base_address + (int64_t)first;
    layout->end = layout->first;
    layout->first_entry_offset = first;
    layout->is_fpkg = header[0] == (uint8_t)'F';
    return true;
}

/* Decode one 28-byte entry header.  Returns false when it is not a well
 * formed entry: header length other than 28, a type outside 0x100..0x103,
 * or a name byte that is not printable ASCII before the first NUL.  The
 * name is copied out with trailing blanks removed (unblob rstrip()s it). */
static bool fpkg_parse_entry(const uint8_t *entry, uint16_t *type, uint16_t *unknown, uint32_t *size, char *name)
{
    size_t length = 0U, index;
    if (xx_data_get_u32(entry, 4, 0, true) != XX_D_LINK_FPKG_CPKG_ENTRY_SIZE) return false;
    *type = xx_data_get_u16(entry + 4U, 2, 0, true);
    *unknown = xx_data_get_u16(entry + 6U, 2, 0, true);
    *size = xx_data_get_u32(entry + 8U, 4, 0, true);
    if (*type < XX_D_LINK_FPKG_CPKG_TYPE_FILE || *type > XX_D_LINK_FPKG_CPKG_TYPE_SIGNATURE) return false;
    for (index = 0U; index < XX_D_LINK_FPKG_CPKG_NAME_SIZE; ++index) {
        uint8_t c = entry[12U + index];
        if (c == 0U) break;
        if (c < 0x20U || c > 0x7eU) return false;
        if (name) name[index] = (char)(c == (uint8_t)'\\' ? '/' : c);
        length = index + 1U;
    }
    while (length != 0U && entry[12U + length - 1U] == (uint8_t)' ') --length;
    if (name) name[length] = 0;
    return true;
}

/* Walk the entries from layout->first.  With items == NULL it only counts;
 * otherwise it fills at most layout->count items (from a counting pass). */
static bool fpkg_walk(Abstractformat *format, fpkg_layout *layout, fpkg_member *items, xx_pd_struct *pd)
{
    int64_t pos = layout->first;
    uint32_t count = 0U;
    const uint32_t limit = items ? layout->count : XX_D_LINK_FPKG_CPKG_MAX_RECORDS;
    while (count < limit && layout->total - pos >= (int64_t)XX_D_LINK_FPKG_CPKG_ENTRY_SIZE) {
        uint8_t entry[XX_D_LINK_FPKG_CPKG_ENTRY_SIZE];
        uint16_t type = 0U, unknown = 0U;
        uint32_t size = 0U;
        int64_t data;
        if ((count & FPKG_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        if (!fpkg_read_at(format->device, pos, entry, sizeof(entry)) || !fpkg_parse_entry(entry, &type, &unknown, &size, items ? items[count].name : NULL)) break;
        data = pos + (int64_t)XX_D_LINK_FPKG_CPKG_ENTRY_SIZE;
        if ((int64_t)size > layout->total - data) break;
        if (items) {
            items[count].header_offset = pos;
            items[count].data_offset = data;
            items[count].size = size;
            items[count].type = type;
            items[count].unknown = unknown;
        }
        ++count;
        pos = data + (int64_t)size;
    }
    if (count == 0U) return false;
    if (items && count != layout->count) return false;
    layout->count = count;
    layout->end = pos;
    return true;
}

/* ---- member names -------------------------------------------------------- */

static uint64_t fpkg_name_hash(const char *name)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (; *name; ++name) {
        uint8_t c = (uint8_t)*name;
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z') c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash ? hash : 1U;
}

/* Open-addressed set of name hashes; true when the hash was newly added. */
static bool fpkg_set_add(uint64_t *table, size_t mask, uint64_t hash)
{
    size_t slot = (size_t)hash & mask, probe;
    for (probe = 0U; probe <= mask; ++probe) {
        if (table[slot] == hash) return false;
        if (table[slot] == 0U) {
            table[slot] = hash;
            return true;
        }
        slot = (slot + 1U) & mask;
    }
    return false;
}

static size_t fpkg_put_number(char *out, uint32_t value)
{
    char digits[10];
    size_t count = 0U, at = 0U;
    do {
        digits[count++] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) out[at++] = digits[--count];
    out[at] = 0;
    return at;
}

/* stem + "_" + n + extension, where the extension starts at the last '.' of
 * the last component (a leading dot does not count). */
static void fpkg_make_variant(const char *base, uint32_t n, char *out)
{
    size_t length = xx_str_len(base), component = 0U, dot, at;
    for (at = 0U; at < length; ++at)
        if (base[at] == '/') component = at + 1U;
    dot = length;
    for (at = length; at > component + 1U; --at)
        if (base[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    xx_rt_memcpy(out, base, dot);
    out[dot] = '_';
    at = dot + 1U + fpkg_put_number(out + dot + 1U, n);
    xx_rt_memcpy(out + at, base + dot, length - dot);
    out[at + length - dot] = 0;
}

static bool fpkg_assign_names(fpkg_member *items, size_t count)
{
    size_t slots = 16U, index;
    uint64_t *table;
    while (slots < count * 2U) slots <<= 1U;
    table = (uint64_t *)xx_mem_calloc(slots, sizeof(*table));
    if (!table) return false;
    for (index = 0U; index < count; ++index) {
        char base[FPKG_NAME_BUFFER];
        uint32_t n;
        if (items[index].name[0] == 0) {
            size_t at;
            xx_rt_memcpy(items[index].name, "entry_", 6U);
            at = 6U + fpkg_put_number(items[index].name + 6U, (uint32_t)index);
            xx_rt_memcpy(items[index].name + at, ".bin", 5U);
        }
        if (fpkg_set_add(table, slots - 1U, fpkg_name_hash(items[index].name))) continue;
        xx_rt_memcpy(base, items[index].name, sizeof(base));
        /* At most `count` names are taken, so count + 1 tries find a free
         * one; the bound keeps a hash-colliding input from spinning. */
        for (n = 1U; n <= (uint32_t)count + 1U; ++n) {
            fpkg_make_variant(base, n, items[index].name);
            if (fpkg_set_add(table, slots - 1U, fpkg_name_hash(items[index].name))) break;
        }
    }
    xx_mem_free(table);
    return true;
}

static bool fpkg_reserved_component(const char *segment, size_t length)
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

static bool fpkg_safe_name(const char *name)
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
            if (length == 0U || segment[length - 1U] == '.' || segment[length - 1U] == ' ' || fpkg_reserved_component(segment, length)) return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- stream -------------------------------------------------------------- */

static void fpkg_stream_free(void *opaque)
{
    fpkg_stream *stream = (fpkg_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool fpkg_open_stream(Abstractformat *format, fpkg_stream **result, xx_pd_struct *pd)
{
    fpkg_layout layout;
    fpkg_member *items = NULL;
    fpkg_stream *stream = NULL;
    if (!result || !fpkg_read_header(format, &layout) || !fpkg_walk(format, &layout, NULL, pd)) return false;
    /* At most 65536 entries: about 4.5 MiB of bookkeeping, never more. */
    items = (fpkg_member *)xx_mem_calloc(layout.count, sizeof(*items));
    stream = (fpkg_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!items || !stream || !fpkg_walk(format, &layout, items, pd) || !fpkg_assign_names(items, layout.count)) {
        if (items) xx_mem_free(items);
        if (stream) xx_mem_free(stream);
        return false;
    }
    stream->items = items;
    stream->count = layout.count;
    *result = stream;
    return true;
}

static bool fpkg_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *fpkg_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool fpkg_set_record(xx_archive_record *record, const fpkg_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)XX_D_LINK_FPKG_CPKG_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---- public API -------------------------------------------------------- */

void xx_d_link_fpkg_cpkg_init(xx_d_link_fpkg_cpkg *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_D_LINK_FPKG_CPKG_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_d_link_fpkg_cpkg_check_is_valid;
    archive->format.handle_base_info = xx_d_link_fpkg_cpkg_handle_base_info;
    archive->format.get_format_size = xx_d_link_fpkg_cpkg_get_format_size;
    archive->format.get_number_of_archive_records = xx_d_link_fpkg_cpkg_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_d_link_fpkg_cpkg_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_d_link_fpkg_cpkg_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_d_link_fpkg_cpkg_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_d_link_fpkg_cpkg_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_d_link_fpkg_cpkg_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_d_link_fpkg_cpkg *xx_d_link_fpkg_cpkg_create(xx_io_device *device, int64_t base_address)
{
    xx_d_link_fpkg_cpkg *archive = (xx_d_link_fpkg_cpkg *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_d_link_fpkg_cpkg_init(archive, device, base_address);
    return archive;
}

void xx_d_link_fpkg_cpkg_destroy(xx_d_link_fpkg_cpkg *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_d_link_fpkg_cpkg_free(xx_d_link_fpkg_cpkg *archive)
{
    if (!archive) return;
    xx_d_link_fpkg_cpkg_destroy(archive);
    xx_mem_free(archive);
}

bool xx_d_link_fpkg_cpkg_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    fpkg_layout layout;
    return fpkg_read_header(format, &layout) && fpkg_walk(format, &layout, NULL, pd);
}

/* The FPKG model name, when the optional block fits before the entries. */
static void fpkg_read_model(Abstractformat *format, const fpkg_layout *layout, char *model)
{
    uint8_t block[8];
    uint8_t raw[XX_D_LINK_FPKG_CPKG_MAX_MODEL];
    uint32_t length, index, kept = 0U;
    model[0] = 0;
    if (!layout->is_fpkg || layout->first_entry_offset < 20U || !fpkg_read_at(format->device, format->base_address + 12, block, sizeof(block))) return;
    length = xx_data_get_u32(block + 4U, 4, 0, true);
    if (length == 0U || length > XX_D_LINK_FPKG_CPKG_MAX_MODEL || length > layout->first_entry_offset - 20U ||
        !fpkg_read_at(format->device, format->base_address + 20, raw, length))
        return;
    for (index = 0U; index < length && raw[index] != 0U; ++index) model[kept++] = (raw[index] >= 0x20U && raw[index] <= 0x7eU) ? (char)raw[index] : '?';
    model[kept] = 0;
}

bool xx_d_link_fpkg_cpkg_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    fpkg_layout layout;
    xx_d_link_fpkg_cpkg *archive;
    if (!fpkg_read_header(format, &layout) || !fpkg_walk(format, &layout, NULL, pd)) return false;
    archive = (xx_d_link_fpkg_cpkg *)format;
    archive->number_of_records = layout.count;
    archive->is_fpkg = layout.is_fpkg;
    archive->first_entry_offset = layout.first_entry_offset;
    archive->archive_end = layout.end;
    fpkg_read_model(format, &layout, archive->model);
    format->number_of_archive_records = layout.count;
    format->format_size = layout.end - format->base_address;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_d_link_fpkg_cpkg_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_d_link_fpkg_cpkg_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_d_link_fpkg_cpkg_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_d_link_fpkg_cpkg_handle_base_info(format, pd)) ? ((xx_d_link_fpkg_cpkg *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_d_link_fpkg_cpkg_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    fpkg_stream *stream;
    xx_archive_record_state *state;
    if (!fpkg_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        fpkg_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = fpkg_stream_free;
    state->total_records = stream->count;
    if (!fpkg_copy_options(&state->options, options) || !fpkg_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_d_link_fpkg_cpkg_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_d_link_fpkg_cpkg_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    fpkg_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (fpkg_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = fpkg_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_d_link_fpkg_cpkg_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    fpkg_stream *stream;
    const fpkg_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !format->device || !state || state->format != format || !state->has_record || !(stream = (fpkg_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = fpkg_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: report whether the member's span is readable. */
        int64_t total = xx_io_total_size(format->device);
        return member->data_offset >= 0 && member->data_offset <= total && (int64_t)member->size <= total - member->data_offset;
    }
    if (!fpkg_safe_name(member->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    /* The helper removes its own output on failure (and only that). */
    result = xx_store_unpack_device_to_file(format->device, member->data_offset, (int64_t)member->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_d_link_fpkg_cpkg_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
