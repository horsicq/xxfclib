/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM ZPAK archives, as shipped by DOS-era IBM/Lotus installers.
 *
 *   header, 8 bytes at offset 0:
 *     0x00  "-ZPAK", 5 bytes
 *     0x05  u8 reserved, always 0
 *     0x06  u16 LE version, always 1
 *
 *   payload, from 0x08 up to the directory: one PKWARE DCL ("implode")
 *   stream per member, back to back, with no per-member header, no
 *   separator and no terminator. A member's data offset is therefore the
 *   running sum of every preceding packedSize, not a stored field.
 *
 *   directory, 88 bytes per member, ending two bytes before EOF:
 *     0x00  char name[80], NUL terminated, the tail zero filled
 *     0x50  u32 LE packedSize
 *     0x54  u16 LE DOS date
 *     0x56  u16 LE DOS time
 *
 *   trailer, the final 2 bytes:
 *     u16 LE member count, which is what locates the directory:
 *     directory_offset = size - 2 - count * 88
 *
 * The container stores no uncompressed size anywhere, so this reader
 * measures each stream with xx_dcl_scan_memory at parse time. That scan is
 * also the format's strongest structural check: it reports how many input
 * bytes the stream really occupies, which must equal the directory's
 * packedSize exactly.
 *
 * There is no checksum of any kind, and no compression method field -- every
 * member is DCL.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ibmzpak/xx_ibmzpak.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_IBMZPAK_COPY_CHUNK (64 * 1024)

typedef struct xx_ibmzpak_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ibmzpak_member;

typedef struct xx_ibmzpak_stream_s {
    xx_ibmzpak_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ibmzpak_stream;

static void xx_ibmzpak_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ibmzpak_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ibmzpak_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ibmzpak_path_safe(const char *name) {
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

static void xx_ibmzpak_stream_free(void *pointer) {
    xx_ibmzpak_stream *stream = (xx_ibmzpak_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ibmzpak_add(xx_ibmzpak_stream *stream,
                          const xx_ibmzpak_member *member) {
    xx_ibmzpak_member *grown = (xx_ibmzpak_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_IBMZPAK_HEADER_SIZE 8
#define XX_IBMZPAK_NAME_SIZE 80
#define XX_IBMZPAK_ENTRY_SIZE 88
#define XX_IBMZPAK_COUNT_SIZE 2
#define XX_IBMZPAK_MIN_PACKED_SIZE 3
#define XX_IBMZPAK_MAX_MEMBERS 65535
#define XX_IBMZPAK_MAX_DECODED (256 * 1024 * 1024)
#define XX_IBMZPAK_VERSION 1U
#define XX_IBMZPAK_DCL_MAX_LITERAL_MODE 1U
#define XX_IBMZPAK_DCL_MIN_DICT_BITS 4U
#define XX_IBMZPAK_DCL_MAX_DICT_BITS 6U
#define XX_IBMZPAK_METHOD_DCL 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ibmzpak_le16(const uint8_t *data);
static uint32_t xx_ibmzpak_le32(const uint8_t *data);
static uint8_t *xx_ibmzpak_load(Abstractformat *self, int64_t data_offset, int64_t size);
static char *xx_ibmzpak_make_name(const uint8_t *field);
static xx_ibmzpak_stream *xx_ibmzpak_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ibmzpak_decode(Abstractformat *self, const xx_ibmzpak_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Two prelude bytes plus at least one byte holding the end-of-stream code. */
/* The count field is a u16, so this is a hard ceiling, not a policy. */
/* PKWARE DCL prelude: literal mode (0 binary / 1 Huffman) then dictionary
 * bits, of which only 4..6 (1K/2K/4K) are legal. The whole corpus writes
 * 0/6; the gate accepts the legal range because the stream format does. */
/* The container has no method field: every member is a DCL stream. The value
 * is synthesised so that 0 keeps its generator-wide meaning of "stored" and a
 * listing never claims these members are uncompressed. */

static uint16_t xx_ibmzpak_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ibmzpak_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Read a whole member into a fresh buffer. Shared by parse (which measures
 * the stream) and decode (which expands it), so the two can never disagree
 * about which bytes belong to a member. */
static uint8_t *xx_ibmzpak_load(Abstractformat *self, int64_t data_offset,
                                int64_t size) {
    uint8_t *packed;

    if (size < XX_IBMZPAK_MIN_PACKED_SIZE ||
        (uint64_t)size > (uint64_t)SIZE_MAX) {
        return NULL;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed) return NULL;
    if (!xx_ibmzpak_read_at(self, data_offset, packed, (size_t)size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* The stored names are either a DOS path rooted at the install target
 * ("\MYDEL.BAT") or a bare 8.3 name space padded inside the fixed buffer
 * ("epfw_dos.pif   "). Returns a '/'-separated copy, or NULL. */
static char *xx_ibmzpak_make_name(const uint8_t *field) {
    char *name;
    uint8_t character;
    size_t terminator = 0U;
    size_t index;
    size_t start;
    size_t begin;
    size_t end;
    size_t out = 0U;
    size_t parts = 0U;
    size_t component = 0U;

    while (terminator < (size_t)XX_IBMZPAK_NAME_SIZE &&
           field[terminator] != 0U) {
        ++terminator;
    }
    /* The field is a fixed, zero-filled 80-byte buffer. A name that fills it
     * edge to edge, or a single stale byte behind the terminator, means this
     * is not that buffer -- with only an eight-byte header to go on, this and
     * the printable-character test are what keep unrelated data from
     * presenting itself as a directory entry. */
    if (terminator == 0U || terminator >= (size_t)XX_IBMZPAK_NAME_SIZE) {
        return NULL;
    }
    for (index = terminator; index < (size_t)XX_IBMZPAK_NAME_SIZE; ++index) {
        if (field[index] != 0U) return NULL;
    }
    for (index = 0U; index < terminator; ++index) {
        character = field[index];
        if (character < 0x20U || character > 0x7EU) return NULL;
        /* '\' is the separator, so the other DOS-illegal punctuation cannot
         * appear in a name the writer produced. */
        if (character == (uint8_t)'/' || character == (uint8_t)':' ||
            character == (uint8_t)'*' || character == (uint8_t)'?' ||
            character == (uint8_t)'"' || character == (uint8_t)'<' ||
            character == (uint8_t)'>' || character == (uint8_t)'|') {
            return NULL;
        }
    }

    /* Normalisation only ever removes bytes, so the field length is enough. */
    name = (char *)xx_mem_alloc(terminator + 1U);
    if (!name) return NULL;

    start = 0U;
    for (index = 0U; index <= terminator; ++index) {
        if (index != terminator && field[index] != (uint8_t)'\\') continue;
        begin = start;
        end = index;
        start = index + 1U;
        /* The trailing blanks come from the writer's fixed 8.3 buffer, not
         * from the name; interior spaces are kept, the padding is not. */
        while (begin < end && field[begin] == (uint8_t)' ') ++begin;
        while (end > begin && field[end - 1U] == (uint8_t)' ') --end;
        if (begin == end) {
            /* A leading '\' is how the archive spells "install root"; an
             * empty component anywhere else is a doubled separator, which the
             * writer never emits. */
            if (component == 0U) {
                ++component;
                continue;
            }
            xx_str_free(name);
            return NULL;
        }
        if ((end - begin == 1U && field[begin] == (uint8_t)'.') ||
            (end - begin == 2U && field[begin] == (uint8_t)'.' &&
             field[begin + 1U] == (uint8_t)'.')) {
            xx_str_free(name);
            return NULL;
        }
        if (parts != 0U) name[out++] = '/';
        while (begin < end) name[out++] = (char)field[begin++];
        ++parts;
        ++component;
    }
    if (parts == 0U) {
        xx_str_free(name);
        return NULL;
    }
    name[out] = '\0';
    return name;
}

static xx_ibmzpak_stream *xx_ibmzpak_parse(Abstractformat *self,
                                           xx_pd_struct *pd) {
    static const uint8_t magic[5] = {'-', 'Z', 'P', 'A', 'K'};
    xx_ibmzpak_stream *stream;
    uint8_t header[XX_IBMZPAK_HEADER_SIZE];
    uint8_t trailer[XX_IBMZPAK_COUNT_SIZE];
    uint8_t entry[XX_IBMZPAK_ENTRY_SIZE];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t directory_size;
    int64_t directory_offset;
    int64_t data_offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_IBMZPAK_HEADER_SIZE + XX_IBMZPAK_MIN_PACKED_SIZE +
                   XX_IBMZPAK_ENTRY_SIZE + XX_IBMZPAK_COUNT_SIZE) {
        return NULL;
    }
    if (!xx_ibmzpak_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* Five magic bytes alone turn up inside unrelated payloads, so the
     * reserved zero and the version word are part of the signature. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    if (header[5] != 0U) return NULL;
    if (xx_ibmzpak_le16(header + 6) != XX_IBMZPAK_VERSION) return NULL;

    if (!xx_ibmzpak_read_at(self, self->base_address + span -
                                      XX_IBMZPAK_COUNT_SIZE,
                            trailer, sizeof(trailer))) {
        return NULL;
    }
    count = (int64_t)xx_ibmzpak_le16(trailer);
    if (count < 1 || count > XX_IBMZPAK_MAX_MEMBERS) return NULL;

    /* count <= 65535 and the entry size is 88, so the product is bounded well
     * below any overflow. */
    directory_size = count * XX_IBMZPAK_ENTRY_SIZE;
    directory_offset = span - XX_IBMZPAK_COUNT_SIZE - directory_size;
    /* The payload must still leave room for one minimal stream, so the
     * directory can never start at or before the fixed header. This is what a
     * stray trailing u16 has to survive before any entry is read. */
    if (directory_offset < XX_IBMZPAK_HEADER_SIZE +
                               XX_IBMZPAK_MIN_PACKED_SIZE) {
        return NULL;
    }

    stream = (xx_ibmzpak_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    data_offset = XX_IBMZPAK_HEADER_SIZE;
    for (index = 0; index < count; ++index) {
        xx_ibmzpak_member member;
        uint8_t *packed;
        char *name;
        int64_t entry_offset;
        int64_t packed_size;
        size_t consumed = 0U;
        size_t produced = 0U;
        bool measured;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = directory_offset + (index * XX_IBMZPAK_ENTRY_SIZE);
        if (!xx_ibmzpak_range_within(span, entry_offset,
                                     XX_IBMZPAK_ENTRY_SIZE) ||
            !xx_ibmzpak_read_at(self, self->base_address + entry_offset, entry,
                                sizeof(entry))) {
            goto fail;
        }

        name = xx_ibmzpak_make_name(entry);
        if (!name) goto fail;

        packed_size = (int64_t)xx_ibmzpak_le32(entry + XX_IBMZPAK_NAME_SIZE);
        if (packed_size < XX_IBMZPAK_MIN_PACKED_SIZE) {
            xx_str_free(name);
            goto fail;
        }
        /* Bounded by the directory, not by EOF: a member whose extent reaches
         * into its own directory is a rejection. */
        if (!xx_ibmzpak_range_within(directory_offset, data_offset,
                                     packed_size)) {
            xx_str_free(name);
            goto fail;
        }

        packed = xx_ibmzpak_load(self, self->base_address + data_offset,
                                 packed_size);
        if (!packed) {
            xx_str_free(name);
            goto fail;
        }
        if (packed[0] > XX_IBMZPAK_DCL_MAX_LITERAL_MODE ||
            packed[1] < XX_IBMZPAK_DCL_MIN_DICT_BITS ||
            packed[1] > XX_IBMZPAK_DCL_MAX_DICT_BITS) {
            xx_mem_free(packed);
            xx_str_free(name);
            goto fail;
        }
        /* The container stores no uncompressed size, so measuring the stream
         * is the only way to get one -- and the measurement doubles as the
         * per-member integrity check: the decoder's own idea of where the
         * bitstream ends must land exactly on the directory's packedSize. */
        measured = xx_dcl_scan_memory(packed, (size_t)packed_size,
                                      (size_t)XX_IBMZPAK_MAX_DECODED,
                                      &consumed, &produced);
        xx_mem_free(packed);
        if (!measured || consumed != (size_t)packed_size ||
            produced > (size_t)XX_IBMZPAK_MAX_DECODED) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = XX_IBMZPAK_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = packed_size;
        member.uncompressed_size = (int64_t)produced;
        member.method = XX_IBMZPAK_METHOD_DCL;
        /* Date at +0x54, time at +0x56, published as the usual packed dword. */
        member.timestamp =
            ((uint64_t)xx_ibmzpak_le16(entry + XX_IBMZPAK_NAME_SIZE + 4) << 16) |
            (uint64_t)xx_ibmzpak_le16(entry + XX_IBMZPAK_NAME_SIZE + 6);
        member.is_folder = false;
        if (!xx_ibmzpak_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        data_offset += packed_size;
    }

    /* The streams carry no headers and no terminators, so the running sum
     * landing exactly on the first directory byte is this container's only
     * whole-file integrity check. Slack here would mean the directory does
     * not describe the payload, and loosening it would let any file whose
     * last two bytes happen to be a plausible count through. */
    if (data_offset != directory_offset) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_ibmzpak_stream_free(stream);
    return NULL;
}


/* Members are PKWARE DCL ("implode") streams with the two-byte prelude still
 * attached; parse has already measured the plaintext length, because the
 * container stores it nowhere. */
static bool xx_ibmzpak_decode(Abstractformat *self,
                              const xx_ibmzpak_member *member, uint8_t **out,
                              size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *plain;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The only method this container has. Anything else came from a parse
     * this decode does not match; treating it as stored would hand the caller
     * compressed bytes dressed up as data. */
    if (member->method != XX_IBMZPAK_METHOD_DCL) return false;
    /* The measured length is still attacker-influenced -- a crafted stream
     * decodes to whatever its bitstream says -- so refuse rather than attempt
     * an unbounded allocation. */
    if (member->uncompressed_size <= 0 ||
        member->uncompressed_size > XX_IBMZPAK_MAX_DECODED ||
        (uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }

    packed = xx_ibmzpak_load(self, member->data_offset,
                             member->compressed_size);
    if (!packed) return false;
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        /* A short decode is the one failure a caller cannot detect once the
         * buffer is handed over, so it is a failure here, never a partial
         * success. */
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

void xx_ibmzpak_init(xx_ibmzpak *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_IBMZPAK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ibm-zpak");
    xx_format_set_extension(&archive->format, "zpk");
    archive->format.check_is_valid = xx_ibmzpak_check_is_valid;
    archive->format.handle_base_info = xx_ibmzpak_handle_base_info;
    archive->format.get_format_size = xx_ibmzpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ibmzpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ibmzpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ibmzpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ibmzpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ibmzpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ibmzpak_free_archive_records_reading;
    archive->format.destroy = xx_ibmzpak_vtable_destroy;
}

xx_ibmzpak *xx_ibmzpak_create(xx_io_device *device, int64_t base_address) {
    xx_ibmzpak *archive = (xx_ibmzpak *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ibmzpak_init(archive, device, base_address);
    return archive;
}

void xx_ibmzpak_destroy(xx_ibmzpak *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ibmzpak_free(xx_ibmzpak *archive) {
    if (!archive) return;
    xx_ibmzpak_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ibmzpak_vtable_destroy(Abstractformat *self) {
    xx_ibmzpak_destroy((xx_ibmzpak *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ibmzpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ibmzpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ibmzpak_parse(self, pd);
    if (!stream) return false;
    xx_ibmzpak_stream_free(stream);
    return true;
}

bool xx_ibmzpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ibmzpak *archive = (xx_ibmzpak *)self;
    xx_ibmzpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ibmzpak_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ibmzpak_stream_free(stream);
    return true;
}

int64_t xx_ibmzpak_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ibmzpak_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ibmzpak *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ibmzpak_set_record(xx_archive_record *record,
                                 const xx_ibmzpak_member *member) {
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

static bool xx_ibmzpak_copy_options(xx_list_s *target,
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

static const xx_var *xx_ibmzpak_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ibmzpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ibmzpak_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ibmzpak_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ibmzpak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ibmzpak_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ibmzpak_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ibmzpak_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ibmzpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ibmzpak_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ibmzpak_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ibmzpak_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ibmzpak_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ibmzpak_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ibmzpak_stream *stream;
    const xx_ibmzpak_member *member;
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
    stream = (xx_ibmzpak_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ibmzpak_path_safe(member->name)) return false;

    path_option = xx_ibmzpak_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ibmzpak_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ibmzpak_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ibmzpak_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
