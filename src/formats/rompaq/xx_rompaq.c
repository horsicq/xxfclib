/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compaq ROMPAQ firmware images.
 *
 *   header, 0x48 bytes at offset 0, words little endian:
 *     0x00  i32      size of the whole ROM image (the plaintext length)
 *     0x04  u32      checksum over the packed bytes
 *     0x08  u16      ROM id; the low 12 bits are the vendor's extension
 *     0x0a  u16      version; MUST be 0x0100 or 0x0101
 *     0x0c  7 bytes  image name; every byte MUST be 0-9 A-Z a-z
 *     0x13  u8       name terminator; MUST be zero
 *     0x14  0x27     ASCII date, NUL padded
 *     0x3b  u8       end of the date field; MUST be zero
 *     0x3c  u8       method: 1 = stored, 2 = PKWARE DCL implode
 *     0x3d  u16      part count; 0 = single part, otherwise a bank chain
 *     0x3f  i32      packed length of part 1; set if and only if the part
 *                    count is set
 *     0x43  u32      reserved; MUST be zero
 *     0x47  u8       reserved; MUST be zero
 *
 * There is no magic. The detector is the set of fixed zero bytes (0x13,
 * 0x3b, 0x43..0x46, 0x47), the two-valued version word, the two-valued
 * method byte, and a name field of seven alphanumerics -- together with the
 * rule that part count and part size are written as a pair or not at all.
 * Any one of those alone would match constantly, which is why the reference
 * publishes no carving signature for this format at all.
 *
 * The payload takes one of three shapes:
 *
 *   - part count non-zero: a bank chain. The container header IS the first
 *     bank header, so the stream starts at offset 0 and covers the whole
 *     file; each bank carries its own 0x48-byte header and its own complete
 *     DCL stream, and only the first header's image size states the total.
 *   - part count zero, method 2: one DCL stream at 0x48, preceded by a
 *     two-byte pad when the word at 0x48 is zero. A non-zero word there is
 *     already the DCL literal-mode/dictionary-bits pair and must stay in
 *     the stream.
 *   - part count zero, method 1: the image stored verbatim at 0x48.
 *
 * The container holds exactly one member, named after the header's name
 * field.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rompaq/xx_rompaq.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/rompaq/xx_rompaq.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_ROMPAQ_COPY_CHUNK (64 * 1024)

typedef struct xx_rompaq_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_rompaq_member;

typedef struct xx_rompaq_stream_s {
    xx_rompaq_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_rompaq_stream;

static void xx_rompaq_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_rompaq_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_rompaq_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_rompaq_path_safe(const char *name) {
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

static void xx_rompaq_stream_free(void *pointer) {
    xx_rompaq_stream *stream = (xx_rompaq_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_rompaq_add(xx_rompaq_stream *stream,
                          const xx_rompaq_member *member) {
    xx_rompaq_member *grown = (xx_rompaq_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ROMPAQ_HEADER_SIZE 0x48
#define XX_ROMPAQ_OFFSET_SIZE 0x00
#define XX_ROMPAQ_OFFSET_ROMID 0x08
#define XX_ROMPAQ_OFFSET_VERSION 0x0a
#define XX_ROMPAQ_OFFSET_NAME 0x0c
#define XX_ROMPAQ_NAME_SIZE 7
#define XX_ROMPAQ_OFFSET_NAME_TERMINATOR 0x13
#define XX_ROMPAQ_OFFSET_DATE_END 0x3b
#define XX_ROMPAQ_OFFSET_METHOD 0x3c
#define XX_ROMPAQ_OFFSET_PARTCOUNT 0x3d
#define XX_ROMPAQ_OFFSET_PARTSIZE 0x3f
#define XX_ROMPAQ_OFFSET_RESERVED1 0x43
#define XX_ROMPAQ_OFFSET_RESERVED2 0x47
#define XX_ROMPAQ_VERSION_100 0x0100U
#define XX_ROMPAQ_VERSION_101 0x0101U
#define XX_ROMPAQ_METHOD_STORED 1U
#define XX_ROMPAQ_METHOD_IMPLODE 2U
#define XX_ROMPAQ_MAX_MEMBERS 1
#define XX_ROMPAQ_MAX_IMAGE_SIZE ((int64_t)0x4000000)
#define XX_ROMPAQ_MAX_STREAM ((int64_t)0x4000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_rompaq_le16(const uint8_t *data);
static uint32_t xx_rompaq_le32(const uint8_t *data);
static bool xx_rompaq_is_name_character(uint8_t character);
static xx_rompaq_stream *xx_rompaq_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_rompaq_decode(Abstractformat *self, const xx_rompaq_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The container's own method numbers, published unchanged. */
/* One image, always. */
/* A ROM image is a handful of megabytes in practice; the ceiling only bounds
 * what a corrupt size field may ask the decode to allocate. */
/* The packed side is bounded separately because a chained image's stream is
 * the WHOLE file: without this a file with a valid header and a gigabyte of
 * tail would be read into memory before the decoder ever refused it. */

static uint16_t xx_rompaq_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_rompaq_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_rompaq_is_name_character(uint8_t character) {
    /* Exactly the class the reference implementation tests: 0-9 A-Z a-z.
     * Widening it to "printable" would cost most of what this format has in
     * place of a magic, since the seven-byte field is otherwise free. */
    return (character >= (uint8_t)'0' && character <= (uint8_t)'9') ||
           (character >= (uint8_t)'A' && character <= (uint8_t)'Z') ||
           (character >= (uint8_t)'a' && character <= (uint8_t)'z');
}

static xx_rompaq_stream *xx_rompaq_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_rompaq_stream *stream = NULL;
    xx_rompaq_member member;
    /* The header plus the two-byte probe the payload path needs. */
    uint8_t header[XX_ROMPAQ_HEADER_SIZE + 2];
    uint8_t selector[2];
    char name_buffer[XX_ROMPAQ_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t image_size;
    int64_t part_size;
    int64_t data_offset;
    int64_t stream_size;
    int32_t index;
    uint32_t method;
    uint16_t version;
    uint16_t part_count;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)sizeof(header)) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_rompaq_read_at(self, self->base_address, header,
                           sizeof(header))) {
        return NULL;
    }

    /* Read as a signed 32-bit value by the reference, so the top bit set is
     * nonsense rather than a four-gigabyte ROM. */
    if (xx_rompaq_le32(header + XX_ROMPAQ_OFFSET_SIZE) > 0x7fffffffU) {
        return NULL;
    }
    image_size = (int64_t)xx_rompaq_le32(header + XX_ROMPAQ_OFFSET_SIZE);
    if (image_size <= 0 || image_size > XX_ROMPAQ_MAX_IMAGE_SIZE) return NULL;

    version = xx_rompaq_le16(header + XX_ROMPAQ_OFFSET_VERSION);
    if (version != (uint16_t)XX_ROMPAQ_VERSION_100 &&
        version != (uint16_t)XX_ROMPAQ_VERSION_101) {
        return NULL;
    }

    method = (uint32_t)header[XX_ROMPAQ_OFFSET_METHOD];
    if (method != XX_ROMPAQ_METHOD_STORED &&
        method != XX_ROMPAQ_METHOD_IMPLODE) {
        return NULL;
    }

    /* The four fixed zero bytes are the backbone of the detector: this
     * format has no magic, and the version word plus the method byte are
     * only three constrained bytes out of 0x48. Dropping any of these -- the
     * reserved dword especially, which looks like slack -- makes the reader
     * claim unrelated binaries. */
    if (header[XX_ROMPAQ_OFFSET_NAME_TERMINATOR] != 0U) return NULL;
    if (header[XX_ROMPAQ_OFFSET_DATE_END] != 0U) return NULL;
    if (header[XX_ROMPAQ_OFFSET_RESERVED2] != 0U) return NULL;
    if (xx_rompaq_le32(header + XX_ROMPAQ_OFFSET_RESERVED1) != 0U) {
        return NULL;
    }

    /* The name field is fixed width: all seven bytes carry name characters
     * and the terminator lives outside it, so the whole field is read. */
    for (index = 0; index < XX_ROMPAQ_NAME_SIZE; ++index) {
        uint8_t character = header[XX_ROMPAQ_OFFSET_NAME + index];

        if (!xx_rompaq_is_name_character(character)) return NULL;
        name_buffer[index] = (char)character;
    }
    name_buffer[XX_ROMPAQ_NAME_SIZE] = '\0';

    part_count = xx_rompaq_le16(header + XX_ROMPAQ_OFFSET_PARTCOUNT);
    if (xx_rompaq_le32(header + XX_ROMPAQ_OFFSET_PARTSIZE) > 0x7fffffffU) {
        return NULL;
    }
    part_size = (int64_t)xx_rompaq_le32(header + XX_ROMPAQ_OFFSET_PARTSIZE);
    /* Part count and part size are written together or not at all. The pair
     * rule is worth more than either field on its own: it is a two-way
     * consistency check across six bytes that a random header fails. */
    if (part_count != 0U) {
        if (part_size <= 0) return NULL;
    } else if (part_size != 0) {
        return NULL;
    }

    if (part_count != 0U) {
        /* The chain restarts at offset 0: the container header IS the first
         * bank header, so the whole file is the stream. */
        if (part_size > span - (int64_t)sizeof(header)) return NULL;
        data_offset = 0;
        stream_size = span;
    } else if (method == XX_ROMPAQ_METHOD_IMPLODE) {
        data_offset = XX_ROMPAQ_HEADER_SIZE;
        /* A zero word here is padding; a non-zero word is already the DCL
         * selector pair and must stay in the stream. */
        if (xx_rompaq_le16(header + XX_ROMPAQ_HEADER_SIZE) == 0U) {
            data_offset += 2;
        }
        if (span - data_offset < 3) return NULL;
        if (!xx_rompaq_read_at(self, self->base_address + data_offset,
                               selector, sizeof(selector))) {
            return NULL;
        }
        /* The DCL prelude: literal mode 0 or 1, dictionary 4..6 bits. Three
         * more constrained bytes, and the only check that looks at the
         * payload at all -- which for a format with no magic is worth
         * keeping even though the decoder would repeat it. */
        if (selector[0] > 1U) return NULL;
        if (selector[1] < 4U || selector[1] > 6U) return NULL;
        stream_size = span - data_offset;
    } else {
        /* Stored: the image must actually be present behind the header. */
        if (span - (int64_t)XX_ROMPAQ_HEADER_SIZE < image_size) return NULL;
        data_offset = XX_ROMPAQ_HEADER_SIZE;
        stream_size = image_size;
    }

    if (stream_size < 1 || stream_size > XX_ROMPAQ_MAX_STREAM) return NULL;
    if (!xx_rompaq_range_within(span, data_offset, stream_size)) return NULL;

    stream = (xx_rompaq_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup(name_buffer);
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = XX_ROMPAQ_HEADER_SIZE;
    member.data_offset = self->base_address + data_offset;
    member.compressed_size = stream_size;
    member.uncompressed_size = image_size;
    /* The container's own method number. The chained/single distinction is
     * NOT folded in here: decode reads the part count back from the header
     * so that a listing shows what the archive says. */
    member.method = method;
    /* The header's date is free-form ASCII, not a packed stamp; there is no
     * numeric timestamp to publish. */
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_rompaq_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = data_offset + stream_size;
    return stream;

fail:
    xx_rompaq_stream_free(stream);
    return NULL;
}


/* The method byte alone does not say how the payload is shaped: a chained
 * image and a single-part image both declare method 2 and differ only in the
 * part count. The count is read back from the container header here rather
 * than smuggled through member->method, so the published method stays the
 * container's own number and this function stays free of side effects. */
static bool xx_rompaq_decode(Abstractformat *self,
                             const xx_rompaq_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t header[XX_ROMPAQ_HEADER_SIZE];
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    uint16_t part_count;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* An unimplemented method must fail rather than fall back to a stored
     * copy: handing a caller imploded firmware labelled as the ROM image is
     * exactly the failure the caller cannot detect. */
    if (member->method != XX_ROMPAQ_METHOD_STORED &&
        member->method != XX_ROMPAQ_METHOD_IMPLODE) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_ROMPAQ_MAX_IMAGE_SIZE) {
        return false;
    }
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_ROMPAQ_MAX_STREAM) {
        return false;
    }

    if (!xx_rompaq_read_at(self, member->header_offset, header,
                           sizeof(header))) {
        return false;
    }
    part_count = xx_rompaq_le16(header + XX_ROMPAQ_OFFSET_PARTCOUNT);

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_rompaq_read_at(self, member->data_offset, input,
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

    if (part_count != 0U) {
        /* The chain decoder is handed the whole file: the first bank header
         * is the container header, and every bank after it carries its own.
         * It succeeds only when the chain lands exactly on the end of the
         * input and produces exactly the image size. */
        ok = xx_rompaq_decode_memory(input, (size_t)member->compressed_size,
                                     output,
                                     (size_t)member->uncompressed_size,
                                     &written);
    } else if (member->method == XX_ROMPAQ_METHOD_IMPLODE) {
        ok = xx_dcl_decode_memory(input, (size_t)member->compressed_size,
                                  output, (size_t)member->uncompressed_size,
                                  &written);
    } else {
        /* Stored: parse only publishes this shape when the two lengths
         * agree, so a disagreement means the member was not built here. */
        ok = (member->compressed_size == member->uncompressed_size);
        if (ok) {
            xx_rt_memcpy(output, input, (size_t)member->uncompressed_size);
            written = (size_t)member->uncompressed_size;
        }
    }

    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_rompaq_init(xx_rompaq *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ROMPAQ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rompaq");
    xx_format_set_extension(&archive->format, "rom");
    archive->format.check_is_valid = xx_rompaq_check_is_valid;
    archive->format.handle_base_info = xx_rompaq_handle_base_info;
    archive->format.get_format_size = xx_rompaq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rompaq_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rompaq_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rompaq_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rompaq_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rompaq_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rompaq_free_archive_records_reading;
    archive->format.destroy = xx_rompaq_vtable_destroy;
}

xx_rompaq *xx_rompaq_create(xx_io_device *device, int64_t base_address) {
    xx_rompaq *archive = (xx_rompaq *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_rompaq_init(archive, device, base_address);
    return archive;
}

void xx_rompaq_destroy(xx_rompaq *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rompaq_free(xx_rompaq *archive) {
    if (!archive) return;
    xx_rompaq_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rompaq_vtable_destroy(Abstractformat *self) {
    xx_rompaq_destroy((xx_rompaq *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rompaq_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rompaq_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_rompaq_parse(self, pd);
    if (!stream) return false;
    xx_rompaq_stream_free(stream);
    return true;
}

bool xx_rompaq_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rompaq *archive = (xx_rompaq *)self;
    xx_rompaq_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_rompaq_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_rompaq_stream_free(stream);
    return true;
}

int64_t xx_rompaq_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rompaq_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_rompaq *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_rompaq_set_record(xx_archive_record *record,
                                 const xx_rompaq_member *member) {
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

static bool xx_rompaq_copy_options(xx_list_s *target,
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

static const xx_var *xx_rompaq_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_rompaq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_rompaq_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_rompaq_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_rompaq_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_rompaq_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_rompaq_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_rompaq_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rompaq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rompaq_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_rompaq_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rompaq_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_rompaq_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rompaq_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_rompaq_stream *stream;
    const xx_rompaq_member *member;
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
    stream = (xx_rompaq_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_rompaq_path_safe(member->name)) return false;

    path_option = xx_rompaq_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_rompaq_decode(self, member, &plain, &plain_size, pd);
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
        !xx_rompaq_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_rompaq_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
