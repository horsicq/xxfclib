/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Megatech Software game resource volumes (".VOL").  Ported from XArchive's
 * games/xmegatechvol.cpp and verified against the 19 samples in
 * F:\ARC\ARC\MEGATECH VOL.
 *
 * There is no magic and no per-member metadata at all.  The file opens with a
 * flat table of little-endian u32 absolute file offsets and nothing else:
 *
 *     0x00   4  u32 LE   offset of member 0, which is also the table's own
 *                        byte length (64, 128, 256 or 512 in the corpus)
 *     0x04   4  u32 LE   offset of member 1
 *     ...              one entry per member, non-decreasing; a member's size
 *                      is table[i + 1] - table[i]
 *                      the last used entry equals the file size and acts as
 *                      the end sentinel; every entry after it is 0
 *
 * Because table[0] doubles as the table length the container is self-checking,
 * and that arithmetic is the whole of the identification: table[0] must be at
 * least 8, a multiple of 4 and inside the file; the entries must be
 * non-decreasing and bounded by the file size until one equals the file size
 * and terminates the walk; and every remaining entry must be 0.  Loosening any
 * of that turns a magicless reader into a wildcard.
 *
 * The container carries no names, so members are numbered from 1 in table
 * order and the extension is picked by peeking at the first four bytes of the
 * body: "Crea" (Creative Voice File) gives ".voc", "GPH\x1d" gives ".gph" and
 * anything else ".bin".  Zero-length gaps - repeated offsets, common in this
 * corpus - are skipped entirely and do not consume a number.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/megatechvol/xx_megatechvol.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef MEGATECHVOL
#define XX_MEGATECHVOL_FILE_TYPE XX_FILE_TYPE_MEGATECHVOL
#else
#define XX_MEGATECHVOL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_MEGATECHVOL_METHOD_STORE 0U
#define XX_MEGATECHVOL_MIN_TABLE 8
/* The table is at most the whole file, and each entry is 4 bytes, so the file
 * size bounds the count long before this cap does. */
#define XX_MEGATECHVOL_MAX_MEMBERS 1048576U
#define XX_MEGATECHVOL_NAME_MAX 32

typedef struct xx_megatechvol_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
} xx_megatechvol_member;

typedef struct xx_megatechvol_stream_s {
    xx_megatechvol_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_megatechvol_stream;

static void xx_megatechvol_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_megatechvol_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_megatechvol_read_at(Abstractformat *self, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_megatechvol_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
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

/* The container stores no names.  Members are numbered from 1 in table order
 * and the extension comes from the body's first four bytes - the same naming
 * the reference implementation produces. */
static char *xx_megatechvol_make_name(uint64_t number, const uint8_t *probe,
                                      size_t probe_size) {
    char text[XX_MEGATECHVOL_NAME_MAX];
    char digits[24];
    const char *extension = ".bin";
    size_t digit_count = 0U;
    size_t length = 0U;

    if (probe_size >= 4U) {
        if (xx_rt_memcmp(probe, "Crea", 4U) == 0) {
            extension = ".voc";
        } else if (probe[0] == 0x47U && probe[1] == 0x50U &&
                   probe[2] == 0x48U && probe[3] == 0x1dU) {
            extension = ".gph";
        }
    }
    do {
        digits[digit_count++] = (char)('0' + (int)(number % 10U));
        number /= 10U;
    } while (number != 0U && digit_count < sizeof(digits));
    while (digit_count != 0U) text[length++] = digits[--digit_count];
    while (*extension) text[length++] = *extension++;
    text[length] = 0;
    return xx_str_dup(text);
}

static void xx_megatechvol_stream_free(void *pointer) {
    xx_megatechvol_stream *stream = (xx_megatechvol_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_megatechvol_stream *xx_megatechvol_parse(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    xx_megatechvol_stream *stream = NULL;
    uint8_t head[4];
    uint8_t *table = NULL;
    int64_t total;
    int64_t span;
    int64_t table_size;
    uint64_t slots;
    uint64_t used;
    uint64_t index;
    uint64_t number;
    int64_t previous;
    bool terminated;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_MEGATECHVOL_MIN_TABLE) return NULL;
    if (!xx_megatechvol_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }

    /* table[0] is the table's own length, so the only number this reader
     * trusts before bounding it is immediately bounded by the file size. */
    table_size = (int64_t)xx_megatechvol_le32(head);
    if (table_size < XX_MEGATECHVOL_MIN_TABLE || table_size % 4 != 0) {
        return NULL;
    }
    if (table_size > span) return NULL;
    slots = (uint64_t)(table_size / 4);
    if (slots > XX_MEGATECHVOL_MAX_MEMBERS) return NULL;

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    if (!table) return NULL;
    if (!xx_megatechvol_read_at(self, self->base_address, table,
                                (size_t)table_size)) {
        goto fail;
    }

    /* Walk the used part of the table: entries must be non-decreasing and
     * inside the file, and the walk ends at the entry that equals the file
     * size.  Everything after it must be zero padding. */
    previous = -1;
    used = 0U;
    terminated = false;
    for (index = 0U; index < slots; ++index) {
        int64_t value = (int64_t)xx_megatechvol_le32(table + (size_t)(index * 4));

        if (value > span || value < previous) goto fail;
        previous = value;
        ++used;
        if (value == span) {
            if (index + 1U == slots ||
                xx_megatechvol_le32(table + (size_t)((index + 1U) * 4)) == 0U) {
                terminated = true;
                break;
            }
        }
    }
    if (!terminated) goto fail;
    for (index = used; index < slots; ++index) {
        if (xx_megatechvol_le32(table + (size_t)(index * 4)) != 0U) goto fail;
    }
    /* "used" counts the end sentinel, so there are used - 1 member slots. */
    if (used < 2U) goto fail;

    stream = (xx_megatechvol_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_megatechvol_member *)xx_mem_alloc(
        sizeof(*stream->items) * (size_t)(used - 1U));
    if (!stream->items) goto fail;
    xx_mem_zero(stream->items, sizeof(*stream->items) * (size_t)(used - 1U));

    number = 0U;
    for (index = 0U; index + 1U < used; ++index) {
        int64_t offset = (int64_t)xx_megatechvol_le32(table + (size_t)(index * 4));
        int64_t next =
            (int64_t)xx_megatechvol_le32(table + (size_t)((index + 1U) * 4));
        int64_t size = next - offset;
        uint8_t probe[4];
        size_t probe_size = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The walk above already proved both offsets lie inside the file and
         * ascend, so the subtraction cannot go negative or escape. */
        if (size <= 0) continue; /* a repeated offset: an empty gap, not a member */
        if (offset < table_size) goto fail;
        if (size >= 4) {
            if (!xx_megatechvol_read_at(self, self->base_address + offset, probe,
                                        sizeof(probe))) {
                goto fail;
            }
            probe_size = sizeof(probe);
        }
        stream->items[stream->count].name =
            xx_megatechvol_make_name(++number, probe, probe_size);
        if (!stream->items[stream->count].name) goto fail;
        stream->items[stream->count].header_offset =
            self->base_address + (int64_t)(index * 4);
        stream->items[stream->count].header_size = 4;
        stream->items[stream->count].data_offset = self->base_address + offset;
        stream->items[stream->count].size = size;
        ++stream->count;
    }
    if (stream->count == 0U) goto fail;

    xx_mem_free(table);
    stream->archive_size = span;
    return stream;

fail:
    if (table) xx_mem_free(table);
    xx_megatechvol_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read.  The length
 * comes from offsets that parse already proved lie inside the file. */
static bool xx_megatechvol_decode(Abstractformat *self,
                            const xx_megatechvol_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->size < 0) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A zero-length member is legitimate - the SINNER corpus carries one -
     * and must extract as an empty file rather than failing the unpack. */
    if (member->size == 0) return true;
    if ((uint64_t)member->size > (uint64_t)SIZE_MAX) return false;
    output = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!output) return false;
    if (!xx_megatechvol_read_at(self, member->data_offset, output,
                          (size_t)member->size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_megatechvol_init(xx_megatechvol *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MEGATECHVOL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-megatech-vol");
    xx_format_set_extension(&archive->format, "vol");
    archive->format.check_is_valid = xx_megatechvol_check_is_valid;
    archive->format.handle_base_info = xx_megatechvol_handle_base_info;
    archive->format.get_format_size = xx_megatechvol_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_megatechvol_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_megatechvol_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_megatechvol_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_megatechvol_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_megatechvol_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_megatechvol_free_archive_records_reading;
    archive->format.destroy = xx_megatechvol_vtable_destroy;
}

xx_megatechvol *xx_megatechvol_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_megatechvol *archive = (xx_megatechvol *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_megatechvol_init(archive, device, base_address);
    return archive;
}

void xx_megatechvol_destroy(xx_megatechvol *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_megatechvol_free(xx_megatechvol *archive) {
    if (!archive) return;
    xx_megatechvol_destroy(archive);
    xx_mem_free(archive);
}

static void xx_megatechvol_vtable_destroy(Abstractformat *self) {
    xx_megatechvol_destroy((xx_megatechvol *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_megatechvol_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_megatechvol_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_megatechvol_parse(self, pd);
    if (!stream) return false;
    xx_megatechvol_stream_free(stream);
    return true;
}

bool xx_megatechvol_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_megatechvol *archive = (xx_megatechvol *)self;
    xx_megatechvol_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_megatechvol_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_megatechvol_stream_free(stream);
    return true;
}

int64_t xx_megatechvol_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_megatechvol_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_megatechvol *)self)->number_of_records : 0U;
}


/* ------------------------------------------------------------- records -- */

static bool xx_megatechvol_set_record(xx_archive_record *record,
                                     const xx_megatechvol_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_MEGATECHVOL_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_megatechvol_copy_options(xx_list_s *target,
                                       const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_megatechvol_get_option(const xx_list_s *options,
                                              uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_megatechvol_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_megatechvol_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_megatechvol_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_megatechvol_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_megatechvol_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_megatechvol_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_megatechvol_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_megatechvol_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_megatechvol_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_megatechvol_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_megatechvol_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_megatechvol_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_megatechvol_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_megatechvol_stream *stream;
    const xx_megatechvol_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_megatechvol_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_megatechvol_path_safe(member->name)) return false;

    path_option = xx_megatechvol_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_megatechvol_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_megatechvol_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_megatechvol_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
