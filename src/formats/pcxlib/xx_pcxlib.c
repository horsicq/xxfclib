/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Genus Microprogramming "pcxLib" picture libraries (".MM1", ".GDL" and
 * numbered extensions in the corpus; PCX and PCC images are what they hold).
 * The layout was taken from Deark's pcxlib module, which XArchive carries as
 * XArchive/Algos/xdearkmodule_misc3_p.cpp, and verified against the 10 samples
 * in F:\ARC\ARC\PCXLIB.
 *
 *   header, 122 bytes at offset 0:
 *     0x00   7  char[7]  magic "pcxLib\0"
 *     0x07   3  bytes    not interpreted
 *     0x0a  50  char[50] copyright banner
 *     0x3c   2  u16 LE   format version (350 in every sample)
 *     0x3e  40  char[40] volume label
 *     0x66  20  bytes    unused
 *
 *   member header, 84 bytes, one per member, each directly followed by the
 *   member body and then by the next member header:
 *     +0x00  1  u8       record tag, always 0x01
 *     +0x01 13  char[13] file name, blank padded 8.3 ("RM15    .PCX")
 *     +0x0e  4  u32 LE   body size
 *     +0x12  4  DOS      modification date/time; not interpreted here
 *     +0x16  2  u16 LE   packing method; 0 (stored) is the only one defined
 *     +0x18 40  char[40] per-member note
 *     +0x40 20  bytes    unused
 *
 * The magic makes identification cheap, but the chain is still validated to
 * the byte: every member header must carry the 0x01 tag and method 0, every
 * body must fit inside the file, and the last body must end exactly at
 * end-of-file.  All 10 corpus samples do.  A non-zero packing method makes the
 * file invalid rather than producing a member this reader could not decode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pcxlib/xx_pcxlib.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef PCXLIB
#define XX_PCXLIB_FILE_TYPE XX_FILE_TYPE_PCXLIB
#else
#define XX_PCXLIB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PCXLIB_METHOD_STORE 0U
#define XX_PCXLIB_HEADER_SIZE 122
#define XX_PCXLIB_ENTRY_SIZE 84
#define XX_PCXLIB_NAME_SIZE 13
#define XX_PCXLIB_TAG 0x01U
/* A member costs 84 bytes of header, so this cap can never be reached before
 * the file-size bound is; it only stops a pathological loop. */
#define XX_PCXLIB_MAX_MEMBERS 200000U

typedef struct xx_pcxlib_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;
} xx_pcxlib_member;

typedef struct xx_pcxlib_stream_s {
    xx_pcxlib_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_pcxlib_stream;

static void xx_pcxlib_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_pcxlib_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_pcxlib_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_pcxlib_read_at(Abstractformat *self, int64_t offset,
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
static bool xx_pcxlib_path_safe(const char *name) {
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

/* The 13 name bytes are a blank-padded 8.3 name, not a C string: dropping the
 * blanks turns "RM15    .PCX" back into "RM15.PCX". */
static char *xx_pcxlib_make_name(const uint8_t *raw) {
    char text[XX_PCXLIB_NAME_SIZE + 1];
    size_t length = 0U;
    size_t index;

    for (index = 0U; index < XX_PCXLIB_NAME_SIZE; ++index) {
        uint8_t c = raw[index];
        if (c == 0x00U) break;
        if (c == 0x20U) continue;
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            c = (uint8_t)'_';
        }
        text[length++] = (char)c;
    }
    while (length != 0U && text[length - 1U] == '.') --length;
    if (length == 0U) text[length++] = '_';
    text[length] = 0;
    return xx_str_dup(text);
}

static void xx_pcxlib_stream_free(void *pointer) {
    xx_pcxlib_stream *stream = (xx_pcxlib_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_pcxlib_stream *xx_pcxlib_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_pcxlib_stream *stream = NULL;
    xx_pcxlib_member *items = NULL;
    uint8_t head[XX_PCXLIB_HEADER_SIZE];
    size_t capacity = 0U;
    size_t count = 0U;
    int64_t total;
    int64_t span;
    int64_t cursor;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PCXLIB_HEADER_SIZE + XX_PCXLIB_ENTRY_SIZE) return NULL;
    if (!xx_pcxlib_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    if (xx_rt_memcmp(head, "pcxLib\0", 7U) != 0) return NULL;

    cursor = XX_PCXLIB_HEADER_SIZE;
    while (cursor < span) {
        uint8_t entry[XX_PCXLIB_ENTRY_SIZE];
        int64_t size;
        uint16_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* Every bound below is against span, the real remaining file size,
         * and is checked before the size is used for anything. */
        if (cursor > span - XX_PCXLIB_ENTRY_SIZE) goto fail;
        if (!xx_pcxlib_read_at(self, self->base_address + cursor, entry,
                               sizeof(entry))) {
            goto fail;
        }
        if (entry[0] != XX_PCXLIB_TAG) goto fail;
        size = (int64_t)xx_pcxlib_le32(entry + 14);
        method = xx_pcxlib_le16(entry + 22);
        /* Method 0 is the only one this container ever defines.  Anything
         * else is rejected rather than published as an undecodable member. */
        if (method != 0U) goto fail;
        if (size < 0 || size > span - cursor - XX_PCXLIB_ENTRY_SIZE) goto fail;
        if (count >= XX_PCXLIB_MAX_MEMBERS) goto fail;

        if (count == capacity) {
            size_t wanted = capacity ? capacity * 2U : 32U;
            xx_pcxlib_member *grown = (xx_pcxlib_member *)xx_mem_alloc(
                sizeof(*grown) * wanted);
            if (!grown) goto fail;
            xx_mem_zero(grown, sizeof(*grown) * wanted);
            if (items) {
                xx_rt_memcpy(grown, items, sizeof(*grown) * count);
                xx_mem_free(items);
            }
            items = grown;
            capacity = wanted;
        }
        items[count].name = xx_pcxlib_make_name(entry + 1);
        if (!items[count].name) goto fail;
        items[count].header_offset = self->base_address + cursor;
        items[count].header_size = XX_PCXLIB_ENTRY_SIZE;
        items[count].data_offset =
            self->base_address + cursor + XX_PCXLIB_ENTRY_SIZE;
        items[count].size = size;
        ++count;
        cursor += XX_PCXLIB_ENTRY_SIZE + size;
    }

    /* The chain has to consume the file exactly; a short or long tail means
     * the walk went wrong and the result cannot be trusted. */
    if (count == 0U || cursor != span) goto fail;

    stream = (xx_pcxlib_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = items;
    stream->count = count;
    stream->archive_size = span;
    return stream;

fail:
    if (items) {
        size_t index;
        for (index = 0U; index < count; ++index) xx_str_free(items[index].name);
        xx_mem_free(items);
    }
    xx_pcxlib_stream_free(stream);
    return NULL;
}

/* Members are stored verbatim, so "decoding" is a bounded read.  The length
 * comes from offsets that parse already proved lie inside the file. */
static bool xx_pcxlib_decode(Abstractformat *self,
                            const xx_pcxlib_member *member, uint8_t **out,
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
    if (!xx_pcxlib_read_at(self, member->data_offset, output,
                          (size_t)member->size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_pcxlib_init(xx_pcxlib *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PCXLIB_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pcxlib");
    xx_format_set_extension(&archive->format, "pcl");
    archive->format.check_is_valid = xx_pcxlib_check_is_valid;
    archive->format.handle_base_info = xx_pcxlib_handle_base_info;
    archive->format.get_format_size = xx_pcxlib_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pcxlib_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pcxlib_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pcxlib_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pcxlib_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pcxlib_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pcxlib_free_archive_records_reading;
    archive->format.destroy = xx_pcxlib_vtable_destroy;
}

xx_pcxlib *xx_pcxlib_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_pcxlib *archive = (xx_pcxlib *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pcxlib_init(archive, device, base_address);
    return archive;
}

void xx_pcxlib_destroy(xx_pcxlib *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pcxlib_free(xx_pcxlib *archive) {
    if (!archive) return;
    xx_pcxlib_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pcxlib_vtable_destroy(Abstractformat *self) {
    xx_pcxlib_destroy((xx_pcxlib *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pcxlib_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcxlib_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_pcxlib_parse(self, pd);
    if (!stream) return false;
    xx_pcxlib_stream_free(stream);
    return true;
}

bool xx_pcxlib_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcxlib *archive = (xx_pcxlib *)self;
    xx_pcxlib_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pcxlib_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_pcxlib_stream_free(stream);
    return true;
}

int64_t xx_pcxlib_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pcxlib_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pcxlib *)self)->number_of_records : 0U;
}


/* ------------------------------------------------------------- records -- */

static bool xx_pcxlib_set_record(xx_archive_record *record,
                                     const xx_pcxlib_member *member) {
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
                                          XX_PCXLIB_METHOD_STORE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_pcxlib_copy_options(xx_list_s *target,
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

static const xx_var *xx_pcxlib_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pcxlib_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pcxlib_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pcxlib_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pcxlib_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pcxlib_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_pcxlib_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_pcxlib_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pcxlib_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pcxlib_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_pcxlib_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pcxlib_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_pcxlib_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pcxlib_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_pcxlib_stream *stream;
    const xx_pcxlib_member *member;
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
    stream = (xx_pcxlib_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_pcxlib_path_safe(member->name)) return false;

    path_option = xx_pcxlib_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_pcxlib_decode(self, member, &plain, &plain_size, pd);
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
        !xx_pcxlib_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_pcxlib_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
