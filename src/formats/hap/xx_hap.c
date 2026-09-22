/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HAP archives (*.hap).
 *
 * Archive header, 15 bytes at the base address:
 *
 *   0x00  91 33 48 46   the archive signature
 *   0x04  11 zero bytes
 *
 * All eleven trailing bytes are zero in every real archive, and the reference
 * detector checks every one of them: a four-byte signature alone is too thin
 * to identify a container that has no other global field.
 *
 * Members follow immediately, each a 40-byte header and then its payload.
 * There is no central directory and no terminator record -- the chain simply
 * runs to the end of the file:
 *
 *   0x00  8E 68 4A 57   the member signature
 *   0x04  i32 LE  compressed size, the payload length
 *   0x08  u32 LE  CRC of the plaintext
 *   0x10  u8      flags, always zero
 *   0x12  u16 LE  DOS time
 *   0x14  u16 LE  DOS date
 *   0x16  i32 LE  uncompressed size
 *   0x1A  13      name, NUL padded, space padded on the right
 *   0x27  u8      method, 0x15 = stored, 0x16 = PPM
 *
 * The next member header starts at payload end. A stored member is a verbatim
 * copy, so its two sizes must be equal; the reference extractor refuses it
 * otherwise.
 *
 * Method 0x16 is a five-order PPM model driven by a 16-bit Witten-Neal-Cleary
 * arithmetic decoder. The header states the plaintext length, so the decoder
 * is always given an exact output size and needs no end marker.
 *
 * The name field can hold bytes that are legal in the field but not in a file
 * name. They are escaped as %XX rather than folded to a single replacement
 * character: escaping is reversible and, unlike folding, cannot collapse two
 * distinct members onto one output path.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hap/xx_hap.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/hap/xx_hap.h"
#include <stdio.h>

#define XX_HAP_COPY_CHUNK (64 * 1024)

typedef struct xx_hap_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_hap_member;

typedef struct xx_hap_stream_s {
    xx_hap_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_hap_stream;

static void xx_hap_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_hap_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_hap_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_hap_path_safe(const char *name) {
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

static void xx_hap_stream_free(void *pointer) {
    xx_hap_stream *stream = (xx_hap_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_hap_add(xx_hap_stream *stream,
                          const xx_hap_member *member) {
    xx_hap_member *grown = (xx_hap_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_HAP_HEADER_SIZE 15
#define XX_HAP_ENTRY_SIZE 40
#define XX_HAP_NAME_OFFSET 26
#define XX_HAP_NAME_SIZE 13
#define XX_HAP_METHOD_STORED 0x15U
#define XX_HAP_METHOD_PPM 0x16U
#define XX_HAP_MAX_MEMBERS 1000000
#define XX_HAP_NAME_BUFFER 40
#define XX_HAP_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_hap_le16(const uint8_t *data);
static uint32_t xx_hap_le32(const uint8_t *data);
static int64_t xx_hap_i32(const uint8_t *data);
static char xx_hap_hex_digit(uint8_t value);
static char *xx_hap_make_name(const uint8_t *entry, size_t index);
static bool xx_hap_entry_is_valid(const uint8_t *entry);
static xx_hap_stream *xx_hap_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_hap_decode(Abstractformat *self, const xx_hap_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No real archive holds a million members; the bound only stops a corrupt
 * chain from spinning. */
/* 12 name bytes, each possibly escaped as %XX, plus the terminator. */

static const uint8_t xx_hap_archive_magic[4] = {0x91U, 0x33U, 0x48U, 0x46U};
static const uint8_t xx_hap_entry_magic[4] = {0x8EU, 0x68U, 0x4AU, 0x57U};

static uint16_t xx_hap_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_hap_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Both size fields are signed in the reference, which then refuses a negative
 * one; sign extending keeps that test meaningful. */
static int64_t xx_hap_i32(const uint8_t *data) {
    return (int64_t)(int32_t)xx_hap_le32(data);
}

static char xx_hap_hex_digit(uint8_t value) {
    return (char)(value < 10U ? ('0' + value) : ('A' + (value - 10U)));
}

/* Trim at the first NUL, then drop trailing spaces, then escape everything
 * that is not a safe path character. The escape is reversible, so two members
 * whose raw names differ cannot end up sharing an output path. */
static char *xx_hap_make_name(const uint8_t *entry, size_t index) {
    char buffer[XX_HAP_NAME_BUFFER];
    const uint8_t *raw = entry + XX_HAP_NAME_OFFSET;
    size_t length = 0U;
    size_t position = 0U;
    size_t cursor;

    while (length < (size_t)XX_HAP_NAME_SIZE - 1U && raw[length] != 0U) {
        ++length;
    }
    while (length > 0U && raw[length - 1U] == ' ') --length;

    for (cursor = 0U; cursor < length; ++cursor) {
        uint8_t value = raw[cursor];
        bool safe = value > 0x20U && value < 0x7FU && value != '%' &&
                    value != '/' && value != '\\' && value != ':' &&
                    value != '*' && value != '?' && value != '"' &&
                    value != '<' && value != '>' && value != '|';
        if (safe) {
            buffer[position++] = (char)value;
        } else {
            buffer[position++] = '%';
            buffer[position++] = xx_hap_hex_digit((uint8_t)(value >> 4));
            buffer[position++] = xx_hap_hex_digit((uint8_t)(value & 0x0FU));
        }
    }
    buffer[position] = '\0';
    /* An all-space or all-NUL field is legal in the container and carries no
     * name at all; the ordinal keeps such members addressable and distinct. */
    if (position == 0U) {
        xx_rt_snprintf(buffer, sizeof(buffer), "record%u", (unsigned)index);
    }
    return xx_str_dup(buffer);
}

/* Exactly the checks the reference extractor makes on a member header, plus
 * the method byte. Because the chain has no terminator and no count, every
 * member header is re-validated from scratch: this per-member signature is
 * what keeps a wrong compressed size from walking the reader into garbage and
 * publishing it. */
static bool xx_hap_entry_is_valid(const uint8_t *entry) {
    uint8_t method;

    if (xx_rt_memcmp(entry, xx_hap_entry_magic,
                     sizeof(xx_hap_entry_magic)) != 0) {
        return false;
    }
    if (entry[16] != 0U) return false;
    if (xx_hap_i32(entry + 4) < 0) return false;
    if (xx_hap_i32(entry + 22) < 0) return false;
    method = entry[39];
    if (method != (uint8_t)XX_HAP_METHOD_STORED &&
        method != (uint8_t)XX_HAP_METHOD_PPM) {
        return false;
    }
    return true;
}

static xx_hap_stream *xx_hap_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_hap_stream *stream = NULL;
    char *name = NULL;
    uint8_t header[XX_HAP_HEADER_SIZE];
    uint8_t entry[XX_HAP_ENTRY_SIZE];
    int64_t total;
    int64_t span;
    int64_t position;
    int64_t compressed_size;
    int64_t uncompressed_size;
    size_t index;
    uint8_t method;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_HAP_HEADER_SIZE + XX_HAP_ENTRY_SIZE) return NULL;
    if (!xx_hap_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, xx_hap_archive_magic,
                     sizeof(xx_hap_archive_magic)) != 0) {
        return NULL;
    }
    /* The eleven bytes behind the signature are all zero in a real archive.
     * They are the other half of the detection: four signature bytes alone
     * match roughly one file in four billion, which over a corpus is not
     * rare enough. */
    for (index = 4U; index < (size_t)XX_HAP_HEADER_SIZE; ++index) {
        if (header[index] != 0U) return NULL;
    }

    stream = (xx_hap_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    position = XX_HAP_HEADER_SIZE;
    while (position < span) {
        xx_hap_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_HAP_MAX_MEMBERS) goto fail;
        if (!xx_hap_range_within(span, position, XX_HAP_ENTRY_SIZE)) goto fail;
        if (!xx_hap_read_at(self, self->base_address + position, entry,
                            sizeof(entry))) {
            goto fail;
        }
        if (!xx_hap_entry_is_valid(entry)) goto fail;

        compressed_size = xx_hap_i32(entry + 4);
        uncompressed_size = xx_hap_i32(entry + 22);
        method = entry[39];
        if (!xx_hap_range_within(span, position + XX_HAP_ENTRY_SIZE,
                                 compressed_size)) {
            goto fail;
        }
        /* A stored member is a verbatim copy, so the two sizes must agree;
         * the reference extractor refuses it otherwise, and without this the
         * extraction path would have to invent which one to believe. */
        if (method == (uint8_t)XX_HAP_METHOD_STORED &&
            compressed_size != uncompressed_size) {
            goto fail;
        }

        name = xx_hap_make_name(entry, stream->count);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + position;
        member.header_size = XX_HAP_ENTRY_SIZE;
        member.data_offset = self->base_address + position + XX_HAP_ENTRY_SIZE;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The container's own method byte, unchanged. */
        member.method = (uint32_t)method;
        member.timestamp = ((uint64_t)xx_hap_le16(entry + 20) << 16) |
                           (uint64_t)xx_hap_le16(entry + 18);
        member.is_folder = false;

        if (!xx_hap_add(stream, &member)) goto fail;
        name = NULL;

        position += XX_HAP_ENTRY_SIZE + compressed_size;
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* The chain defines the archive: whatever lies past the last payload is
     * not part of it. */
    stream->archive_size = position;
    return stream;

fail:
    xx_str_free(name);
    xx_hap_stream_free(stream);
    return NULL;
}


/* The header's plaintext length is attacker-controlled, so it is capped
 * before it becomes an allocation. */

static bool xx_hap_decode(Abstractformat *self, const xx_hap_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > (int64_t)XX_HAP_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > (int64_t)XX_HAP_MAX_DECODED) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!packed) return false;
    if (member->compressed_size != 0 &&
        !xx_hap_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == (uint32_t)XX_HAP_METHOD_STORED) {
        /* Parse already refused a stored member whose sizes disagree; this
         * repeats it because the alternative is handing the caller fewer
         * bytes than it was told to expect. */
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = (size_t)member->compressed_size;
        return true;
    }
    if (member->method != (uint32_t)XX_HAP_METHOD_PPM) {
        /* A method byte the format may define but this reader does not
         * implement. Copying the payload through as stored would hand back
         * PPM-coded bytes that the caller cannot recognise as wrong. */
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0
                                        ? (size_t)member->uncompressed_size
                                        : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The order-5 PPM decoder produces exactly the declared length; a short
     * decode is corruption, never a partial success. */
    if (!xx_hap_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_hap_init(xx_hap *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_HAP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hap");
    xx_format_set_extension(&archive->format, "hap");
    archive->format.check_is_valid = xx_hap_check_is_valid;
    archive->format.handle_base_info = xx_hap_handle_base_info;
    archive->format.get_format_size = xx_hap_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hap_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hap_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hap_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hap_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hap_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hap_free_archive_records_reading;
    archive->format.destroy = xx_hap_vtable_destroy;
}

xx_hap *xx_hap_create(xx_io_device *device, int64_t base_address) {
    xx_hap *archive = (xx_hap *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_hap_init(archive, device, base_address);
    return archive;
}

void xx_hap_destroy(xx_hap *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_hap_free(xx_hap *archive) {
    if (!archive) return;
    xx_hap_destroy(archive);
    xx_mem_free(archive);
}

static void xx_hap_vtable_destroy(Abstractformat *self) {
    xx_hap_destroy((xx_hap *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_hap_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_hap_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_hap_parse(self, pd);
    if (!stream) return false;
    xx_hap_stream_free(stream);
    return true;
}

bool xx_hap_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hap *archive = (xx_hap *)self;
    xx_hap_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_hap_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_hap_stream_free(stream);
    return true;
}

int64_t xx_hap_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_hap_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_hap *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_hap_set_record(xx_archive_record *record,
                                 const xx_hap_member *member) {
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

static bool xx_hap_copy_options(xx_list_s *target,
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

static const xx_var *xx_hap_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_hap_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_hap_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_hap_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_hap_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_hap_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_hap_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_hap_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_hap_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_hap_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_hap_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_hap_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_hap_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hap_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_hap_stream *stream;
    const xx_hap_member *member;
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
    stream = (xx_hap_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_hap_path_safe(member->name)) return false;

    path_option = xx_hap_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_hap_decode(self, member, &plain, &plain_size, pd);
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
        !xx_hap_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_hap_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
