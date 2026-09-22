/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZX Spectrum SCL disk images.
 *
 *   header, 9 bytes at offset 0:
 *     0x00  "SINCLAIR"          8-byte signature
 *     0x08  u8 file count       1..255; zero is refused
 *
 *   catalogue entry, 14 bytes, count of them starting at offset 9:
 *     0x00  name, 8 bytes, space padded
 *     0x08  u8 TR-DOS file type letter ('B', 'C', 'D', ...)
 *     0x09  u16 LE start address / first parameter
 *     0x0b  u16 LE length in bytes / second parameter
 *     0x0d  u8 length in 256-byte sectors
 *
 * The data area starts directly behind the catalogue and every file occupies
 * exactly sectors * 256 bytes, in catalogue order, with no per-file header
 * and no padding. That makes the layout fully implied by the catalogue: there
 * is nothing to walk and nothing to resynchronise on.
 *
 * A file extracted from an SCL is written as a 17-byte Hobeta header followed
 * by its sectors, because the catalogue entry is the only place the TR-DOS
 * name, type and length live and a bare sector dump loses all of it. The
 * header is the entry's first 13 bytes, a zero byte, the sector count, and a
 * 16-bit checksum; the decode is therefore prefix-then-copy, which is exactly
 * what the SCL-sectors pseudo codec does.
 *
 * Truncated images genuinely occur - an image cut short still lists its whole
 * catalogue - so the last member's extent is clamped to end-of-file rather
 * than rejecting the catalogue. The eight-byte signature is what makes that
 * safe.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/scl/xx_scl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/sclsectors/xx_sclsectors.h"

#include <stdio.h>

#define XX_SCL_COPY_CHUNK (64 * 1024)

typedef struct xx_scl_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_scl_member;

typedef struct xx_scl_stream_s {
    xx_scl_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_scl_stream;

static void xx_scl_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_scl_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_scl_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_scl_path_safe(const char *name) {
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

static void xx_scl_stream_free(void *pointer) {
    xx_scl_stream *stream = (xx_scl_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_scl_add(xx_scl_stream *stream,
                          const xx_scl_member *member) {
    xx_scl_member *grown = (xx_scl_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_SCL_HEADER_SIZE 9
#define XX_SCL_ENTRY_SIZE 14
#define XX_SCL_SECTOR_SIZE 256
#define XX_SCL_PREFIX_SIZE 17
#define XX_SCL_MAX_MEMBERS 255
#define XX_SCL_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static bool xx_scl_signature(const uint8_t *header);
static bool xx_scl_entry_name(const uint8_t *entry, char **out_name);
static xx_scl_stream *xx_scl_parse(Abstractformat *self, xx_pd_struct *pd);
static void xx_scl_hobeta_prefix(const uint8_t *entry, uint8_t sectors, uint8_t *prefix);
static bool xx_scl_decode(Abstractformat *self, const xx_scl_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static bool xx_scl_signature(const uint8_t *header) {
    static const char magic[8] = {'S', 'I', 'N', 'C', 'L', 'A', 'I', 'R'};

    /* Eight fixed bytes are the entire false-positive defence. The catalogue
     * that follows is 14 bytes of free-form TR-DOS fields with no reserved
     * words, no checksum and no terminator, and the data area is implied
     * rather than described, so there is nothing else to cross-check. This
     * is the check a later reader will be tempted to loosen to seven bytes
     * or to a case-insensitive compare; doing so is what makes the format
     * match arbitrary text files. */
    return xx_rt_memcmp(header, magic, sizeof(magic)) == 0;
}

/* "NAME    " + type letter -> "NAME.B".
 *
 * TR-DOS pads the name with spaces and stores the type as a single byte that
 * is normally a letter but is a raw code on some disks; the reference maps a
 * control byte onto '_' rather than dropping the extension, because the byte
 * still distinguishes two files that share a name. Everything else must be
 * printable ASCII: a catalogue full of high bytes is not a catalogue. */
static bool xx_scl_entry_name(const uint8_t *entry, char **out_name) {
    char buffer[11];
    size_t end = 8U;
    size_t index;
    uint8_t type;
    char *name;

    *out_name = NULL;
    for (index = 0U; index < 8U; ++index) {
        uint8_t character = entry[index];
        if (character < 0x20U || character > 0x7EU) return false;
        /* A separator inside a TR-DOS name would turn one member into a
         * directory path; the format has no directories. */
        if (character == '/' || character == '\\') return false;
        buffer[index] = (char)character;
    }
    while (end > 0U && buffer[end - 1U] == ' ') --end;
    /* A blank name is not a file. */
    if (end == 0U) return false;

    type = entry[8];
    if (type > 0x7EU) return false;
    buffer[end] = '.';
    buffer[end + 1U] = (type < 0x20U) ? '_' : (char)type;
    buffer[end + 2U] = '\0';
    if (buffer[end + 1U] == '/' || buffer[end + 1U] == '\\') return false;

    name = xx_str_dup(buffer);
    if (!name) return false;
    *out_name = name;
    return true;
}

static xx_scl_stream *xx_scl_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_scl_stream *stream;
    uint8_t header[XX_SCL_HEADER_SIZE];
    uint8_t entry[XX_SCL_ENTRY_SIZE];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t catalogue_size;
    int64_t data_offset;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_SCL_HEADER_SIZE) return NULL;
    if (!xx_scl_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (!xx_scl_signature(header)) return NULL;

    count = (int64_t)header[8];
    /* An image declaring no files is not an image. */
    if (count <= 0 || count > XX_SCL_MAX_MEMBERS) return NULL;
    catalogue_size = count * XX_SCL_ENTRY_SIZE;
    /* The catalogue is fixed size and must be present in full; a truncation
     * that cuts into it means the count byte is not a count. */
    if (!xx_scl_range_within(span, XX_SCL_HEADER_SIZE, catalogue_size)) {
        return NULL;
    }

    stream = (xx_scl_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    data_offset = XX_SCL_HEADER_SIZE + catalogue_size;
    for (index = 0; index < count; ++index) {
        xx_scl_member member;
        int64_t entry_offset = XX_SCL_HEADER_SIZE + index * XX_SCL_ENTRY_SIZE;
        int64_t data_size;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_scl_read_at(self, self->base_address + entry_offset, entry,
                            sizeof(entry))) {
            goto fail;
        }
        data_size = (int64_t)entry[13] * XX_SCL_SECTOR_SIZE;

        /* Truncated images do occur, so the extent is clamped rather than
         * rejected - but only downwards, and only once the start is known to
         * be inside the file. A member whose data even begins past EOF ends
         * the catalogue instead of being published at a bogus offset. */
        if (!xx_scl_range_within(span, data_offset, data_size)) {
            if (data_offset > span) break;
            data_size = span - data_offset;
        }

        if (!xx_scl_entry_name(entry, &name)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = XX_SCL_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        /* The 17-byte Hobeta header is synthesised, not stored, so the member
         * is longer than its sectors by exactly that much. */
        member.uncompressed_size = data_size + XX_SCL_PREFIX_SIZE;
        /* TR-DOS stores nothing compressed and no method field. */
        member.method = 0U;
        /* The catalogue carries no timestamps and no directory entries. */
        member.timestamp = 0U;
        member.is_folder = false;

        if (!xx_scl_path_safe(name) || !xx_scl_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        data_offset += data_size;
    }

    if (stream->count == 0U) goto fail;
    /* Trailing bytes behind the last sector are an overlay, not a member. */
    stream->archive_size = data_offset < span ? data_offset : span;
    return stream;

fail:
    xx_scl_stream_free(stream);
    return NULL;
}


/* 13 entry bytes, a zero byte, the sector count, and the checksum word. */
/* The file count is a single byte, so this is the format's own ceiling. */
/* 255 files of 255 sectors is well under a megabyte; the cap only exists so
 * a crafted catalogue cannot ask for an unbounded buffer. */

/* Build the Hobeta header the image does not store.
 *
 * The checksum is the byte sum of the 15 bytes in front of it, multiplied by
 * 0x101 and biased by 0x69, truncated to 16 bits. The multiply is what makes
 * it a checksum rather than a parity byte, and getting it wrong produces a
 * file every Spectrum emulator loads and every Hobeta tool rejects. */
static void xx_scl_hobeta_prefix(const uint8_t *entry, uint8_t sectors,
                                 uint8_t *prefix) {
    uint16_t checksum = 0U;
    size_t index;

    for (index = 0U; index < 13U; ++index) prefix[index] = entry[index];
    prefix[13] = 0U;
    prefix[14] = sectors;
    for (index = 0U; index < 15U; ++index) {
        checksum = (uint16_t)(checksum + (uint16_t)prefix[index]);
    }
    checksum = (uint16_t)((uint16_t)(checksum * 0x101U) + 0x69U);
    prefix[15] = (uint8_t)(checksum & 0xFFU);
    prefix[16] = (uint8_t)((checksum >> 8) & 0xFFU);
}

/* Nothing here is compressed: the member is its sectors, preceded by a
 * header synthesised from the catalogue entry. The entry is re-read rather
 * than carried on the member because the member record has no room for a
 * properties blob, and re-reading it keeps the decode free of side effects. */
static bool xx_scl_decode(Abstractformat *self, const xx_scl_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t entry[XX_SCL_ENTRY_SIZE];
    uint8_t prefix[XX_SCL_PREFIX_SIZE];
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The catalogue carries no method field at all, so method is 0 for every
     * member and a non-zero value means the parse and the decode disagree. */
    if (member->method != 0U) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_SCL_MAX_DECODED ||
        member->uncompressed_size > XX_SCL_MAX_DECODED) {
        return false;
    }
    /* The published length is the prefix plus the (possibly clamped) sector
     * extent; anything else means the two halves disagree. */
    if (member->uncompressed_size !=
        member->compressed_size + XX_SCL_PREFIX_SIZE) {
        return false;
    }

    if (!xx_scl_read_at(self, member->header_offset, entry, sizeof(entry))) {
        return false;
    }
    xx_scl_hobeta_prefix(entry, entry[13], prefix);

    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 &&
        !xx_scl_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_sclsectors_decode_memory_ex(prefix, (size_t)XX_SCL_PREFIX_SIZE,
                                        input,
                                        (size_t)member->compressed_size,
                                        output,
                                        (size_t)member->uncompressed_size,
                                        &written) ||
        written != (size_t)member->uncompressed_size) {
        /* Short output reported as success is the one failure the caller
         * cannot detect. */
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_scl_init(xx_scl *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SCL;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-spectrum-scl");
    xx_format_set_extension(&archive->format, "scl");
    archive->format.check_is_valid = xx_scl_check_is_valid;
    archive->format.handle_base_info = xx_scl_handle_base_info;
    archive->format.get_format_size = xx_scl_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_scl_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_scl_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_scl_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_scl_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_scl_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_scl_free_archive_records_reading;
    archive->format.destroy = xx_scl_vtable_destroy;
}

xx_scl *xx_scl_create(xx_io_device *device, int64_t base_address) {
    xx_scl *archive = (xx_scl *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_scl_init(archive, device, base_address);
    return archive;
}

void xx_scl_destroy(xx_scl *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_scl_free(xx_scl *archive) {
    if (!archive) return;
    xx_scl_destroy(archive);
    xx_mem_free(archive);
}

static void xx_scl_vtable_destroy(Abstractformat *self) {
    xx_scl_destroy((xx_scl *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_scl_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_scl_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_scl_parse(self, pd);
    if (!stream) return false;
    xx_scl_stream_free(stream);
    return true;
}

bool xx_scl_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_scl *archive = (xx_scl *)self;
    xx_scl_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_scl_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_scl_stream_free(stream);
    return true;
}

int64_t xx_scl_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_scl_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_scl *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_scl_set_record(xx_archive_record *record,
                                 const xx_scl_member *member) {
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

static bool xx_scl_copy_options(xx_list_s *target,
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

static const xx_var *xx_scl_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_scl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_scl_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_scl_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_scl_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_scl_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_scl_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_scl_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_scl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_scl_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_scl_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_scl_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_scl_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_scl_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_scl_stream *stream;
    const xx_scl_member *member;
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
    stream = (xx_scl_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_scl_path_safe(member->name)) return false;

    path_option = xx_scl_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_scl_decode(self, member, &plain, &plain_size, pd);
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
        !xx_scl_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_scl_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
