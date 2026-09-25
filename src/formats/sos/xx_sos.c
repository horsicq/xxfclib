/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SOS bootable Amiga disk images.
 *
 * The container is a whole floppy image -- a whole number of 512-byte sectors,
 * at least thirty-one of them -- that begins with an AmigaDOS bootblock:
 *
 *   +0x00  "DOS"
 *   +0x03  u8 filesystem flag, 0..5
 *   +0x10  "SOS1", the loader's own version tag
 *
 * There is no pointer to the directory anywhere in the bootblock. The
 * directory is found by probing the start of sectors 1..30 (0x200..0x3C00) for
 * a record whose name is "loader"; that record is the first directory entry as
 * well as the anchor.
 *
 * Directory records are 32 bytes, laid out big-endian and consecutive:
 *
 *   +0x00  i32 BE data offset from the start of the image
 *   +0x04  i32 BE data size
 *   +0x08  24 bytes of name, NUL-terminated only when shorter than the field
 *
 * The table ends at the first record whose data offset is zero or whose name
 * is empty; that record is not a member and carries no other meaning.
 *
 * Every member is stored verbatim. There are no timestamps, no checksums and
 * no folders, so a record's only integrity check before the data is read is
 * that its extent lies inside the image.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sos/xx_sos.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SOS_COPY_CHUNK (64 * 1024)

typedef struct xx_sos_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_sos_member;

typedef struct xx_sos_stream_s {
    xx_sos_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_sos_stream;

static void xx_sos_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_sos_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_sos_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_sos_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_sos_stream_free(void *pointer) {
    xx_sos_stream *stream = (xx_sos_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_sos_add(xx_sos_stream *stream,
                          const xx_sos_member *member) {
    xx_sos_member *grown = (xx_sos_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_sos_decode(Abstractformat *self,
                             const xx_sos_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_sos_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_SOS_SECTOR_SIZE 512
#define XX_SOS_BOOTBLOCK_SIZE 0x14
#define XX_SOS_RECORD_SIZE 32
#define XX_SOS_NAME_SIZE 24
#define XX_SOS_FIRST_PROBE_SECTOR 1
#define XX_SOS_LAST_PROBE_SECTOR 0x1E
#define XX_SOS_MAX_DOS_FLAG 5
#define XX_SOS_MAX_MEMBERS 100000

static uint32_t xx_sos_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/* The name occupies the whole 24-byte field when it is that long, so there is
 * not always a NUL to stop at. */
static size_t xx_sos_name_length(const uint8_t *record) {
    size_t length = 0U;

    while (length < (size_t)XX_SOS_NAME_SIZE && record[8 + length] != 0U) {
        ++length;
    }
    return length;
}

static xx_sos_stream *xx_sos_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_sos_stream *stream;
    uint8_t boot[XX_SOS_BOOTBLOCK_SIZE];
    uint8_t record[XX_SOS_RECORD_SIZE];
    char name[XX_SOS_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t directory_offset = -1;
    int64_t offset;
    int32_t sector;
    size_t length;
    size_t i;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A floppy image is a whole number of sectors and has to be long enough
     * for the directory probe to have all thirty sectors to look at. Both are
     * cheap shape checks that a truncated or padded file fails. */
    if (span < (int64_t)(XX_SOS_LAST_PROBE_SECTOR + 1) * XX_SOS_SECTOR_SIZE) {
        return NULL;
    }
    if ((span % XX_SOS_SECTOR_SIZE) != 0) return NULL;

    if (!xx_sos_read_at(self, self->base_address, boot, sizeof(boot))) {
        return NULL;
    }
    if (xx_rt_memcmp(boot, "DOS", 3) != 0) return NULL;
    /* AmigaDOS filesystem flag; values above 5 were never defined. */
    if (boot[3] > XX_SOS_MAX_DOS_FLAG) return NULL;
    /* "SOS1" at +0x10 is what separates this from every other bootable Amiga
     * disk -- "DOS" alone matches all of them, so this is the signature. */
    if (xx_rt_memcmp(boot + 0x10, "SOS1", 4) != 0) return NULL;

    for (sector = XX_SOS_FIRST_PROBE_SECTOR;
         sector <= XX_SOS_LAST_PROBE_SECTOR; ++sector) {
        int64_t probe = (int64_t)sector * XX_SOS_SECTOR_SIZE;

        if (pd && xx_pd_is_stopped(pd)) return NULL;
        if (!xx_sos_range_within(span, probe, XX_SOS_RECORD_SIZE)) break;
        if (!xx_sos_read_at(self, self->base_address + probe, record,
                            sizeof(record))) {
            return NULL;
        }
        /* Nothing points at the directory: it is located by its first record
         * being named "loader" at the very start of a sector. Together with
         * "SOS1" this is the whole of the format's identification, and a
         * sector-aligned exact-length match is what keeps it narrow. */
        if (xx_sos_name_length(record) == 6U &&
            xx_rt_memcmp(record + 8, "loader", 6) == 0) {
            directory_offset = probe;
            break;
        }
    }
    if (directory_offset < 0) return NULL;

    stream = (xx_sos_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = directory_offset;
    for (;;) {
        xx_sos_member member;
        int64_t data_offset;
        int64_t size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* Running out of image before a terminating record is reached is a
         * damaged directory, not a clean end. */
        if (!xx_sos_range_within(span, offset, XX_SOS_RECORD_SIZE)) goto fail;
        if (!xx_sos_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }

        /* Both fields are signed 32-bit in the original, so a word with the
         * top bit set is a negative value, not a four-gigabyte one. */
        data_offset = (int64_t)(int32_t)xx_sos_be32(record);
        size = (int64_t)(int32_t)xx_sos_be32(record + 4);
        length = xx_sos_name_length(record);
        /* A zero data offset or an empty name ends the table. Both are a
         * clean stop in the original, not an error. */
        if (data_offset == 0) break;
        if (length == 0U) break;
        if (data_offset < 0 || size < 0) goto fail;
        /* The only per-member integrity field this format has: the extent must
         * lie inside the image. */
        if (!xx_sos_range_within(span, data_offset, size)) goto fail;
        if (stream->count >= (size_t)XX_SOS_MAX_MEMBERS) goto fail;
        /* The reference decodes the name as Latin-1 and accepts every byte.
         * These are Amiga filenames, ASCII in practice, and a control byte
         * here means the probe has locked onto noise that merely happened to
         * spell "loader" a sector earlier. */
        for (i = 0U; i < length; ++i) {
            if (record[8 + i] < 0x20U || record[8 + i] > 0x7EU) goto fail;
        }
        for (i = 0U; i < length; ++i) name[i] = (char)record[8 + i];
        name[length] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_SOS_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        if (!xx_sos_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        offset += XX_SOS_RECORD_SIZE;
    }

    if (stream->count == 0U) goto fail;
    /* The members never fill the floppy, but the image IS the container, so
     * its whole span is the format size. */
    stream->archive_size = span;
    return stream;

fail:
    xx_sos_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_sos_init(xx_sos *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_SOS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sos-adf");
    xx_format_set_extension(&archive->format, "adf");
    archive->format.check_is_valid = xx_sos_check_is_valid;
    archive->format.handle_base_info = xx_sos_handle_base_info;
    archive->format.get_format_size = xx_sos_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sos_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sos_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sos_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sos_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sos_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sos_free_archive_records_reading;
    archive->format.destroy = xx_sos_vtable_destroy;
}

xx_sos *xx_sos_create(xx_io_device *device, int64_t base_address) {
    xx_sos *archive = (xx_sos *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_sos_init(archive, device, base_address);
    return archive;
}

void xx_sos_destroy(xx_sos *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sos_free(xx_sos *archive) {
    if (!archive) return;
    xx_sos_destroy(archive);
    xx_mem_free(archive);
}

static void xx_sos_vtable_destroy(Abstractformat *self) {
    xx_sos_destroy((xx_sos *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_sos_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sos_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_sos_parse(self, pd);
    if (!stream) return false;
    xx_sos_stream_free(stream);
    return true;
}

bool xx_sos_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sos *archive = (xx_sos *)self;
    xx_sos_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_sos_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_sos_stream_free(stream);
    return true;
}

int64_t xx_sos_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sos_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sos *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sos_set_record(xx_archive_record *record,
                                 const xx_sos_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_sos_copy_options(xx_list_s *target,
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

static const xx_var *xx_sos_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_sos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sos_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_sos_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_sos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_sos_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_sos_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_sos_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sos_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_sos_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sos_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_sos_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sos_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_sos_stream *stream;
    const xx_sos_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sos_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_sos_path_safe(member->name)) return false;

    path_option = xx_sos_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_sos_decode(self, member, &plain, &plain_size, pd);
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

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_sos_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_sos_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
