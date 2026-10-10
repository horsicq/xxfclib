/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SSBOB slideshow: a descriptor table followed by fixed 74-byte records.
 * Payload ranges are stored verbatim and belong to the preceding named
 * record.  Marker strings and executable content are not interpreted.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fss/xx_fss.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#ifdef FSS
#define XX_FSS_FILE_TYPE XX_FILE_TYPE_FSS
#else
#define XX_FSS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The table-fits-in-the-file check is what actually bounds the allocation. */
#define XX_FSS_MAX_SLOTS 1000000U

typedef struct xx_fss_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint32_t crc32;
    uint32_t method;
    bool has_crc;
    bool is_folder;
} xx_fss_member;

typedef struct xx_fss_stream_s {
    xx_fss_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_fss_stream;

static void xx_fss_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_fss_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 || xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_fss_path_safe(const char *path)
{
    const char *cursor = path;

    if (!path || !path[0] || path[0] == '/') return false;
    if (path[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* Build a filesystem-safe name from raw 8-bit bytes.  Backslashes become
 * path separators, everything a filesystem would object to becomes '_'. */
static char *xx_fss_make_name(const uint8_t *raw, size_t size, bool keep_path)
{
    char *text;
    size_t length = 0U;
    size_t index;

    if (!raw && size != 0U) return NULL;
    text = (char *)xx_mem_alloc(size + 2U);
    if (!text) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if ((c == '/' || c == '\\') && keep_path) {
            if (length != 0U && text[length - 1U] == '/') continue;
            text[length++] = '/';
            continue;
        }
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            text[length++] = '_';
        } else {
            text[length++] = (char)c;
        }
    }
    while (length != 0U && (text[length - 1U] == ' ' || text[length - 1U] == '.' || text[length - 1U] == '/')) {
        --length;
    }
    while (length != 0U && text[0] == '/') {
        xx_rt_memmove(text, text + 1, length - 1U);
        --length;
    }
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return text;
}

static void xx_fss_stream_free(void *pointer)
{
    xx_fss_stream *stream = (xx_fss_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Grow the member vector one entry at a time.  The caller has already bounded
 * the member count against the real file size, so this cannot be driven to an
 * unbounded allocation by a small header. */
static bool xx_fss_add(xx_fss_stream *stream, const xx_fss_member *member)
{
    if (!stream || !member) return false;
    if (stream->count == stream->capacity) {
        size_t wanted = stream->capacity ? stream->capacity * 2U : 16U;
        xx_fss_member *grown;
        if (wanted > SIZE_MAX / sizeof(*grown)) return false;
        grown = (xx_fss_member *)xx_mem_realloc(stream->items, wanted * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = wanted;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Slots carry no names; they are filed under a zero-padded index, the width
 * taken from the slot count the way the reference reader's listing does it. */
static XXFC_MAYBE_UNUSED char *xx_fss_slot_name(uint32_t index, uint32_t width)
{
    char text[32];
    size_t length = 0U;
    uint32_t scale = 1U;
    uint32_t digits = 1U;

    while (digits < width && scale <= 100000000U) {
        scale *= 10U;
        ++digits;
    }
    while (index / scale >= 10U && scale <= 100000000U) scale *= 10U;
    for (; scale != 0U; scale /= 10U) {
        text[length++] = (char)('0' + ((index / scale) % 10U));
    }
    text[length++] = '.';
    text[length++] = 'b';
    text[length++] = 'i';
    text[length++] = 'n';
    text[length] = 0;
    return xx_str_dup(text);
}

static XXFC_MAYBE_UNUSED uint32_t xx_fss_digits(uint32_t value)
{
    uint32_t digits = 1U;

    while (value >= 10U) {
        value /= 10U;
        ++digits;
    }
    return digits;
}

/* The header advertises the descriptor count and 74-byte record count.
 * Each nonempty range belongs to the name in the preceding record; a record
 * can simultaneously close one file and name the next.  The terminal offset
 * must equal the physical end of the file. */
static xx_fss_stream *xx_fss_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fss_stream *stream = NULL;
    uint8_t head[16], previous[74], entry[74], sentinel[8];
    int64_t span, total, table_start, data_start, running;
    uint32_t descriptors, count, i;
    if (!self || !self->device || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < 98 || span > 256 * 1024 * 1024 || !xx_fss_read_at(self, self->base_address, head, sizeof(head)) || xx_rt_memcmp(head, "SSBOB", 5U) || head[5] != 1U ||
        head[6] || head[7] != 1U || head[9] || head[11] || head[12] || head[13] || head[14] != 0xffU || head[15] != 0xffU)
        return NULL;
    descriptors = head[8];
    count = head[10];
    if (descriptors < 2U || descriptors > 128U || count < 2U || count > 128U) return NULL;
    table_start = 16 + (int64_t)(descriptors - 1U) * 12;
    data_start = table_start + (int64_t)count * 74 + 8;
    if (data_start >= span || !xx_fss_read_at(self, self->base_address + data_start - 8, sentinel, sizeof(sentinel)) ||
        xx_data_get_u32(sentinel, 4, 0, false) != (uint32_t)span || xx_data_get_u32(sentinel + 4, 4, 0, false) != 0U ||
        !xx_fss_read_at(self, self->base_address + table_start, previous, sizeof(previous)))
        return NULL;
    stream = (xx_fss_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    running = data_start;
    for (i = 1U; i < count; ++i) {
        xx_fss_member member;
        int64_t offset, size, prev_offset, prev_size;
        size_t start = 9U, length = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_fss_read_at(self, self->base_address + table_start + (int64_t)i * 74, entry, sizeof(entry))) goto fail;
        offset = (int64_t)xx_data_get_u32(entry, 4, 0, false);
        size = (int64_t)xx_data_get_u32(entry + 4, 4, 0, false);
        prev_offset = (int64_t)xx_data_get_u32(previous, 4, 0, false);
        prev_size = (int64_t)xx_data_get_u32(previous + 4, 4, 0, false);
        if (size && (previous[8] != 0x10U || offset != running || offset != prev_offset + prev_size || offset < data_start || offset > span || size > span - offset))
            goto fail;
        if (size) {
            while (start < 73U && previous[start] && previous[start] != '\\' && previous[start] != '/') ++start;
            while (start < 73U && previous[start]) {
                if (previous[start] == '\\' || previous[start] == '/') length = start + 1U;
                ++start;
            }
            start = length ? length : 9U;
            length = 0U;
            while (start + length < 73U && previous[start + length]) ++length;
            if (!length || start + length == 73U) goto fail;
            xx_mem_zero(&member, sizeof(member));
            member.name = xx_fss_make_name(previous + start, length, false);
            if (!member.name || !xx_fss_path_safe(member.name)) {
                xx_str_free(member.name);
                goto fail;
            }
            member.header_offset = self->base_address + table_start + (int64_t)(i - 1U) * 74;
            member.header_size = 74;
            member.data_offset = self->base_address + offset;
            member.packed_size = size;
            member.unpacked_size = (uint64_t)size;
            if (!xx_fss_add(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
            running = offset + size;
        }
        xx_mem_copy(previous, entry, sizeof(entry));
    }
    if (!stream->count || running != span) goto fail;
    stream->archive_size = span;
    return stream;
fail:
    xx_fss_stream_free(stream);
    return NULL;
}

static bool xx_fss_decode(Abstractformat *self, const xx_fss_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *bytes;
    size_t size;
    if (!self || !member || !out || !out_size || member->packed_size < 0 || (uint64_t)member->packed_size > SIZE_MAX || member->packed_size > 256 * 1024 * 1024 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    size = (size_t)member->packed_size;
    bytes = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!bytes) return false;
    if (size && !xx_fss_read_at(self, member->data_offset, bytes, size)) {
        xx_mem_free(bytes);
        return false;
    }
    *out = bytes;
    *out_size = size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fss_init(xx_fss *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FSS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-fss");
    xx_format_set_extension(&archive->format, "fss");
    archive->format.check_is_valid = xx_fss_check_is_valid;
    archive->format.handle_base_info = xx_fss_handle_base_info;
    archive->format.get_format_size = xx_fss_get_format_size;
    archive->format.get_number_of_archive_records = xx_fss_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_fss_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_fss_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_fss_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_fss_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_fss_free_archive_records_reading;
    archive->format.destroy = xx_fss_vtable_destroy;
}

xx_fss *xx_fss_create(xx_io_device *device, int64_t base_address)
{
    xx_fss *archive = (xx_fss *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_fss_init(archive, device, base_address);
    return archive;
}

void xx_fss_destroy(xx_fss *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_fss_free(xx_fss *archive)
{
    if (!archive) return;
    xx_fss_destroy(archive);
    xx_mem_free(archive);
}

static void xx_fss_vtable_destroy(Abstractformat *self)
{
    xx_fss_destroy((xx_fss *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_fss_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fss_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_fss_parse(self, pd);
    if (!stream) return false;
    xx_fss_stream_free(stream);
    return true;
}

bool xx_fss_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fss *archive = (xx_fss *)self;
    xx_fss_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_fss_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_fss_stream_free(stream);
    return true;
}

int64_t xx_fss_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fss_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_fss *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_fss_set_record(xx_archive_record *record, const xx_fss_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    if (member->has_crc && !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, member->crc32)) {
        return false;
    }
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->is_folder) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_fss_copy_options(xx_list_s *target, const xx_list_s *options)
{
    size_t index;

    if (!options) return true;
    if (!target) return false;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) || !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_fss_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_fss_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_fss_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_fss_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_fss_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_fss_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_fss_copy_options(&state->options, options) || (stream->count != 0U && !xx_fss_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fss_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_fss_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_fss_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fss_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_fss_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_fss_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_fss_stream *stream;
    const xx_fss_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fss_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_fss_path_safe(member->name)) return false;

    path_option = xx_fss_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_fss_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) || !xx_fss_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_fss_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
