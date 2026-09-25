/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZXZIP - the ZX Spectrum "ZIP" archiver.  This is COMPRESSION, not
 * encryption: no member is keyed and nothing here takes a password.
 *
 * ARCHIVE HEADER (0x11 bytes)
 *   +0x00  8    archive name; every byte must be >= 0x20
 *   +0x08  3    "ZIP"
 *   +0x0b  u16  total size of the (entry + data) chain that follows
 *   +0x0d  u8   zero
 *   +0x0e  u8   high-byte guard: (u16 at +0x0b) >> 8 must be <= this byte
 *   +0x0f  u16  Hobeta-style checksum of bytes 0..0x0e:
 *               (sum(bytes) & 0xffff) * 0x101 + 0x69, truncated to 16 bits
 *
 * DIRECTORY ENTRY (0x16 bytes), immediately followed by its packed data
 *   +0x00  8    TR-DOS file name
 *   +0x08  1    TR-DOS type letter
 *   +0x09  u16  "start" (the BASIC program length for type B/b)
 *   +0x0b  u16  "length"
 *   +0x0d  u8   sector count
 *   +0x0e  u16  packed size
 *   +0x10  u32  checksum of the decompressed data (opaque; not verified here)
 *   +0x14  u8   method: 0 store, 1 unsupported, 2 PKZIP Shrink, 3 ZXZIP LZH
 *   +0x15  u8   sub-method, used by method 3
 *
 * A MEMBER IS NOT EMITTED AS ITS OWN BYTES.  xx_zxzip_decode_memory() writes a
 * 17-byte HOBETA header built from the entry, then the data, then zero padding
 * up to the entry's sector count - every method goes through it, stored
 * members included.  So the uncompressed size published per record is
 * XX_ZXZIP_HOBETA_SIZE + the padded data size, not the payload size, and the
 * entry has to be carried verbatim from the directory walk into the decode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zxzip/xx_zxzip.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zxzip/xx_zxzip.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_ZXZIP_HEADER_SIZE 0x11
#define XX_ZXZIP_DIR_ENTRY_SIZE XX_ZXZIP_ENTRY_SIZE
/* The chain length field is 16 bit and every member costs at least an entry,
 * so 4096 is already far past anything a real producer can emit. */
#define XX_ZXZIP_MAX_MEMBERS 4096U
/* name(8) + '.' + '$' + type + NUL */
#define XX_ZXZIP_NAME_BUFFER 12U

typedef struct xx_zxzip_member_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size; /**< XX_ZXZIP_HOBETA_SIZE + padded size */
    uint32_t crc;
    uint8_t method;
    uint8_t sub_method;
    uint8_t entry[XX_ZXZIP_DIR_ENTRY_SIZE];
} xx_zxzip_member;

typedef struct xx_zxzip_stream_s {
    xx_zxzip_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zxzip_stream;

static void xx_zxzip_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_zxzip_read16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_zxzip_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool xx_zxzip_read_at(Abstractformat *self, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t completed = 0U;
    if (!self || !self->device || offset < 0 || (!buffer && size != 0U) ||
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

static bool xx_zxzip_range_within(int64_t total, int64_t offset,
                                  int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* The Hobeta check the archive header carries over its own first 15 bytes. */
static uint16_t xx_zxzip_hobeta_check(const uint8_t *data, size_t count) {
    uint32_t sum = 0U;
    size_t index;
    for (index = 0U; index < count; ++index) sum += data[index];
    return (uint16_t)(((sum & 0xffffU) * 0x101U + 0x69U) & 0xffffU);
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zxzip_path_safe(const char *name) {
    const char *cursor = name;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/' && *end != '\\') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/*
 * TR-DOS names are eight characters plus a one-letter type and the emitted
 * file is a Hobeta image, whose usual naming is "name.$<type>".  The eight
 * name bytes are only guaranteed to be >= 0x20, so the punctuation Windows
 * reserves is folded to '_' - the alternative, dropping the member, would
 * silently lose files the reference lists.
 */
static char *xx_zxzip_member_name(const uint8_t *entry, size_t index) {
    char buffer[XX_ZXZIP_NAME_BUFFER];
    size_t length = 8U;
    size_t position;
    unsigned char type;
    while (length > 0U && (entry[length - 1U] == ' ' ||
                           entry[length - 1U] == '.')) {
        --length;
    }
    for (position = 0U; position < length; ++position) {
        unsigned char ch = entry[position];
        bool safe = ch > 0x20U && ch < 0x7fU && ch != '/' && ch != '\\' &&
                    ch != ':' && ch != '*' && ch != '?' && ch != '"' &&
                    ch != '<' && ch != '>' && ch != '|';
        buffer[position] = safe ? (char)ch : '_';
    }
    if (length == 0U) {
        /* An all-blank name still identifies a member; number it instead. */
        buffer[0] = 'f';
        buffer[1] = (char)('0' + (char)((index / 10U) % 10U));
        buffer[2] = (char)('0' + (char)(index % 10U));
        length = 3U;
    }
    type = entry[8];
    if (type <= 0x20U || type >= 0x7fU || type == '/' || type == '\\' ||
        type == ':' || type == '*' || type == '?' || type == '"' ||
        type == '<' || type == '>' || type == '|') {
        type = '_';
    }
    buffer[length] = '.';
    buffer[length + 1U] = '$';
    buffer[length + 2U] = (char)type;
    buffer[length + 3U] = '\0';
    return xx_str_dup(buffer);
}

static void xx_zxzip_stream_free(void *pointer) {
    xx_zxzip_stream *stream = (xx_zxzip_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name on success. */
static bool xx_zxzip_add(xx_zxzip_stream *stream,
                         const xx_zxzip_member *member) {
    xx_zxzip_member *grown;
    if (!stream || !member || stream->count >= XX_ZXZIP_MAX_MEMBERS ||
        stream->count + 1U > SIZE_MAX / sizeof(*grown)) {
        return false;
    }
    grown = (xx_zxzip_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_zxzip_stream *xx_zxzip_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_zxzip_stream *stream;
    uint8_t header[XX_ZXZIP_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t remaining;
    unsigned index;

    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return NULL;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_ZXZIP_HEADER_SIZE + XX_ZXZIP_DIR_ENTRY_SIZE)) {
        return NULL;
    }
    if (!xx_zxzip_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* "ZIP" at +8, the zero at +0x0d, the high-byte guard at +0x0e and the
     * Hobeta check over bytes 0..0x0e.  Three bytes of magic in the middle of
     * a record would match far too often on their own; the checksum is what
     * makes this a detector. */
    if (xx_rt_memcmp(header + 8, "ZIP", 3U) != 0 || header[0x0d] != 0U) {
        return NULL;
    }
    remaining = (int64_t)xx_zxzip_read16(header + 0x0b);
    if ((remaining >> 8) > (int64_t)header[0x0e]) return NULL;
    if (xx_zxzip_hobeta_check(header, 15U) !=
        xx_zxzip_read16(header + 0x0f)) {
        return NULL;
    }
    for (index = 0U; index < 9U; ++index) {
        if (header[index] < 0x20U) return NULL;
    }

    stream = (xx_zxzip_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;

    offset = XX_ZXZIP_HEADER_SIZE;
    while (remaining > 0) {
        uint8_t entry[XX_ZXZIP_DIR_ENTRY_SIZE];
        xx_zxzip_member member;
        int64_t data_offset;
        int64_t packed;
        size_t data_size = 0U;
        size_t padded_size = 0U;
        bool truncated = false;
        bool name_ok = true;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= XX_ZXZIP_MAX_MEMBERS) break;
        if (!xx_zxzip_range_within(span, offset,
                                   (int64_t)XX_ZXZIP_DIR_ENTRY_SIZE)) {
            break;
        }
        if (!xx_zxzip_read_at(self, self->base_address + offset, entry,
                              sizeof(entry))) {
            goto fail;
        }
        for (index = 0U; index < 9U; ++index) {
            if (entry[index] < 0x20U) name_ok = false;
        }
        if (!name_ok) break;

        packed = (int64_t)xx_zxzip_read16(entry + 0x0e);
        data_offset = offset + (int64_t)XX_ZXZIP_DIR_ENTRY_SIZE;
        /* The last member of a truncated archive declares more packed bytes
         * than the file holds.  It is still listed - the reference does - but
         * its extraction then fails, because a short codec stream cannot fill
         * the member out. */
        if (!xx_zxzip_range_within(span, data_offset, packed)) {
            if (data_offset >= span) break;
            packed = span - data_offset;
            truncated = true;
        }
        if (!xx_zxzip_member_size(entry, sizeof(entry), &data_size,
                                  &padded_size)) {
            break;
        }
        if (padded_size > (size_t)(INT64_MAX - XX_ZXZIP_HOBETA_SIZE)) break;
        (void)data_size;

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = self->base_address + offset;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = packed;
        member.uncompressed_size =
            (int64_t)XX_ZXZIP_HOBETA_SIZE + (int64_t)padded_size;
        member.crc = xx_zxzip_read32(entry + 0x10);
        member.method = entry[0x14];
        member.sub_method = entry[0x15];
        xx_rt_memcpy(member.entry, entry, sizeof(entry));
        member.name = xx_zxzip_member_name(entry, stream->count);
        if (!member.name) goto fail;
        if (!xx_zxzip_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + packed;
        remaining -= packed + (int64_t)XX_ZXZIP_DIR_ENTRY_SIZE;
        if (truncated) break;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = offset < span ? offset : span;
    return stream;
fail:
    xx_zxzip_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/*
 * The whole member - the entry verbatim plus its packed bytes - goes to the
 * codec in one call; there is no streaming entry point and no way to emit the
 * payload without the Hobeta wrapper the codec builds.
 */
static bool xx_zxzip_decode(Abstractformat *self,
                            const xx_zxzip_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t input_size;
    size_t output_size;
    size_t written = 0U;

    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (!self || !member || !out || !out_size || member->compressed_size < 0 ||
        member->uncompressed_size <= 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->uncompressed_size >
            (uint64_t)XX_ZXZIP_MAX_UNCOMPRESSED_SIZE ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    input_size = (size_t)member->compressed_size;
    output_size = (size_t)member->uncompressed_size;
    /* xx_mem_alloc(0) is not a usable buffer; a member with no packed bytes
     * still has to hand the codec a valid pointer. */
    input = (uint8_t *)xx_mem_alloc(input_size != 0U ? input_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!input || !output ||
        !xx_zxzip_read_at(self, member->data_offset, input, input_size) ||
        (pd && xx_pd_is_stopped(pd)) ||
        !xx_zxzip_decode_memory(input, input_size, member->entry,
                                sizeof(member->entry), output, output_size,
                                &written) ||
        written != output_size) {
        xx_mem_free(input);
        xx_mem_free(output);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = output_size;
    return true;
}

/* ------------------------------------------------------------ lifetime -- */

void xx_zxzip_init(xx_zxzip *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    /* Registration pending: xxfc_defs.h is shared and out of scope here, so
     * the file type stays generic until XX_FILE_TYPE_ZXZIP lands. */
    archive->format.file_type = XX_FILE_TYPE_ZXZIP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zxzip");
    xx_format_set_extension(&archive->format, "$z");
    archive->format.check_is_valid = xx_zxzip_check_is_valid;
    archive->format.handle_base_info = xx_zxzip_handle_base_info;
    archive->format.get_format_size = xx_zxzip_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zxzip_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zxzip_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zxzip_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zxzip_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zxzip_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zxzip_free_archive_records_reading;
    archive->format.destroy = xx_zxzip_vtable_destroy;
    archive->archive_size = -1;
}

xx_zxzip *xx_zxzip_create(xx_io_device *device, int64_t base_address) {
    xx_zxzip *archive = (xx_zxzip *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_zxzip_init(archive, device, base_address);
    return archive;
}

void xx_zxzip_destroy(xx_zxzip *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->archive_size = -1;
}

static void xx_zxzip_vtable_destroy(Abstractformat *self) {
    xx_zxzip_destroy((xx_zxzip *)self);
}

void xx_zxzip_free(xx_zxzip *archive) {
    if (!archive) return;
    xx_zxzip_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_zxzip_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zxzip_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zxzip_parse(self, pd);
    if (!stream) return false;
    xx_zxzip_stream_free(stream);
    return true;
}

bool xx_zxzip_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zxzip *archive = (xx_zxzip *)self;
    xx_zxzip_stream *stream;
    int64_t total;

    if (!self) return false;
    self->base_info_handled = true;
    stream = xx_zxzip_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        archive->number_of_records = 0U;
        archive->archive_size = -1;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->archive_size = stream->archive_size;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    xx_zxzip_stream_free(stream);
    return true;
}

int64_t xx_zxzip_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zxzip_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zxzip *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zxzip_set_record(xx_archive_record *record,
                                const xx_zxzip_member *member) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)XX_ZXZIP_DIR_ENTRY_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_zxzip_copy_options(xx_list_s *target,
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

static const xx_var *xx_zxzip_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zxzip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zxzip_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zxzip_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zxzip_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zxzip_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zxzip_copy_options(&state->options, options) ||
        !xx_zxzip_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zxzip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zxzip_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_zxzip_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zxzip_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_zxzip_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zxzip_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_zxzip_stream *stream;
    const xx_zxzip_member *member;
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
    stream = (xx_zxzip_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zxzip_path_safe(member->name)) return false;

    path_option =
        xx_zxzip_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_zxzip_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_zxzip_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;
        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_zxzip_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_zxzip_get_number_of_records(const xx_zxzip *archive) {
    return archive ? archive->number_of_records : 0U;
}
