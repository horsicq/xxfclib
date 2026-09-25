/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/uimage/xx_uimage.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma_alone/xx_lzma_alone.h"
#include "xxfclib/algo/lzop/xx_lzop.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/formats/gz/xx_gz.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_UIMAGE exists in the enum. */
#ifdef UIMAGE
#define XX_UIMAGE_FILE_TYPE XX_FILE_TYPE_UIMAGE
#else
#define XX_UIMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_UIMAGE_MAGIC UINT32_C(0x27051956)
#define XX_UIMAGE_HEADER_SIZE 64U
#define XX_UIMAGE_NAME_SIZE 32U
#define XX_UIMAGE_HCRC_OFFSET 4U

/* A multi-file image's component list is bounded by the payload it lives in,
 * but a payload that is all size words would still describe a great many
 * tiny members; this is the hard stop. */
#define XX_UIMAGE_MAX_MEMBERS 1024U

/** Streaming buffer for the payload CRC pass. */
#define XX_UIMAGE_STAGING_SIZE 65536U

typedef struct xx_uimage_member_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    uint8_t compression;  /**< Effective compressor for THIS member. */
} xx_uimage_member;

typedef struct xx_uimage_private_s {
    xx_uimage_member *members;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t data_offset;
    int64_t archive_end;
    uint32_t header_crc;
    uint32_t data_crc;
    uint32_t timestamp;
    uint32_t data_size;
    uint32_t load_address;
    uint32_t entry_point;
    uint8_t os;
    uint8_t cpu_arch;
    uint8_t image_type;
    uint8_t compression;
    char name[XX_UIMAGE_NAME_SIZE + 1U];
} xx_uimage_private;

typedef struct xx_uimage_archive_stream_s {
    xx_uimage_private parsed;
    size_t index;
} xx_uimage_archive_stream;

static void xx_uimage_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: a uImage payload can exceed 2 GiB in
 * principle and long is 32-bit on Win64. */
static bool xx_uimage_read_at(xx_io_device *device, int64_t offset, void *data,
                              size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_uimage_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_uimage_range_within(int64_t total_size, int64_t offset,
                                   int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_uimage_private_cleanup(xx_uimage_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->members[index].name) {
            xx_str_free(parsed->members[index].name);
        }
    }
    if (parsed->members) xx_mem_free(parsed->members);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->data_offset = -1;
    parsed->archive_end = -1;
}

static bool xx_uimage_append_member(xx_uimage_private *parsed,
                                    xx_uimage_member *member) {
    xx_uimage_member *grown;
    size_t capacity;
    if (!parsed || !member || !member->name ||
        parsed->count >= XX_UIMAGE_MAX_MEMBERS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 8U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->members)) {
            return false;
        }
        grown = (xx_uimage_member *)xx_mem_realloc(
            parsed->members, capacity * sizeof(*parsed->members));
        if (!grown) return false;
        parsed->members = grown;
        parsed->capacity = capacity;
    }
    parsed->members[parsed->count++] = *member;
    xx_mem_zero(member, sizeof(*member));
    return true;
}

/* Extraction-time check: the name must stay inside the destination tree on
 * every host this library builds for, so the reserved Windows punctuation is
 * rejected here even though ih_name may legally carry it. */
static bool xx_uimage_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' &&
                 component[1] == '.') ||
                component[length - 1U] == ' ' ||
                component[length - 1U] == '.') {
                return false;
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

/* Build a "<prefix><index>" member name without pulling in snprintf. */
static char *xx_uimage_indexed_name(const char *prefix, size_t index) {
    char digits[24];
    size_t used = 0U;
    size_t prefix_size = prefix ? xx_str_len(prefix) : 0U;
    char *name;
    size_t position;
    if (index == 0U) {
        digits[used++] = '0';
    } else {
        char reversed[24];
        size_t length = 0U;
        while (index != 0U && length < sizeof(reversed)) {
            reversed[length++] = (char)('0' + (index % 10U));
            index /= 10U;
        }
        while (length != 0U) digits[used++] = reversed[--length];
    }
    name = (char *)xx_mem_alloc(prefix_size + used + 1U);
    if (!name) return NULL;
    if (prefix_size != 0U) xx_rt_memcpy(name, prefix, prefix_size);
    for (position = 0U; position < used; ++position) {
        name[prefix_size + position] = digits[position];
    }
    name[prefix_size + used] = '\0';
    return name;
}

/* Copy ih_name out of the header, trimming the NUL padding, and reduce it to
 * a single safe path component.  An empty or unusable ih_name falls back to
 * the supplied default. */
static char *xx_uimage_member_name(const char *image_name,
                                   const char *fallback) {
    char buffer[XX_UIMAGE_NAME_SIZE + 1U];
    size_t used = 0U;
    size_t index;
    for (index = 0U; index < XX_UIMAGE_NAME_SIZE; ++index) {
        unsigned char ch = (unsigned char)image_name[index];
        if (ch == 0U) break;
        /* A separator or a control byte would turn one member name into a
         * path; both are folded to an underscore rather than rejected, so a
         * descriptive ih_name is not lost over one stray character. */
        if (ch < 32U || ch == '/' || ch == '\\' || ch == ':' || ch == '<' ||
            ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*') {
            ch = (unsigned char)'_';
        }
        buffer[used++] = (char)ch;
    }
    /* A trailing dot or space is legal here but names an unopenable file on
     * Windows, so it is trimmed away. */
    while (used != 0U && (buffer[used - 1U] == ' ' ||
                          buffer[used - 1U] == '.')) {
        --used;
    }
    buffer[used] = '\0';
    if (used == 0U) return xx_str_create(fallback);
    return xx_str_create(buffer);
}

/* CRC32 over a device range, streamed so a large payload is never resident. */
static bool xx_uimage_crc_range(xx_io_device *device, int64_t offset,
                                int64_t size, uint32_t *out_crc,
                                xx_pd_struct *pd) {
    uint8_t staging[XX_UIMAGE_STAGING_SIZE];
    uint32_t crc = 0U;
    if (!device || !out_crc || offset < 0 || size < 0) return false;
    if (size != 0 && xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (size > 0) {
        size_t step = (size < (int64_t)sizeof(staging)) ? (size_t)size
                                                        : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        crc = xx_crc32_calc(crc, staging, step);
        size -= (int64_t)step;
    }
    *out_crc = crc;
    return true;
}

/* Split an IH_TYPE_MULTI payload.  The component size list is a run of big
 * endian u32 values closed by a zero word; the components follow it, each
 * padded up to a 4-byte boundary.  Padding on the LAST component is optional
 * in practice, so the tail is allowed to fall short of its aligned length. */
static bool xx_uimage_parse_multi_members(Abstractformat *self,
                                          xx_uimage_private *parsed) {
    uint32_t sizes[XX_UIMAGE_MAX_MEMBERS];
    size_t count = 0U;
    int64_t cursor = parsed->data_offset;
    int64_t payload_end;
    int64_t component;
    uint64_t needed = 0U;
    size_t index;
    if (!xx_uimage_add(parsed->data_offset, parsed->data_size,
                       &payload_end)) {
        return false;
    }
    for (;;) {
        uint8_t word[4];
        uint32_t value;
        if (payload_end - cursor < 4) return false;
        if (!xx_uimage_read_at(self->device, cursor, word, sizeof(word))) {
            return false;
        }
        cursor += 4;
        value = xx_data_get_u32(word, sizeof(word), 0U, true);
        if (value == 0U) break;  /* terminator */
        if (count >= XX_UIMAGE_MAX_MEMBERS) return false;
        sizes[count++] = value;
        /* Each component is padded up to a 4-byte boundary; account for that
         * while checking the declared total against the real payload. */
        needed += ((uint64_t)value + 3U) & ~(uint64_t)3U;
        if (needed > (uint64_t)INT64_MAX) return false;
    }
    /* An empty list is a multi image with no components, which no producer
     * emits and which would publish nothing; treat it as malformed. */
    if (count == 0U) return false;
    /* The last component's padding may be absent, so the shortfall allowance
     * is the padding of that final component and nothing more. */
    {
        uint64_t slack = ((uint64_t)sizes[count - 1U] + 3U) &
                         ~(uint64_t)3U;
        slack -= sizes[count - 1U];
        if ((uint64_t)(payload_end - cursor) + slack < needed) return false;
    }
    component = cursor;
    for (index = 0U; index < count; ++index) {
        xx_uimage_member member;
        xx_mem_zero(&member, sizeof(member));
        if (!xx_uimage_range_within(payload_end, component,
                                    (int64_t)sizes[index])) {
            return false;
        }
        member.name = xx_uimage_indexed_name("image", index);
        member.data_offset = component;
        member.data_size = (int64_t)sizes[index];
        /* ih_comp describes the payload as a whole and a split image is only
         * ever reached with IH_COMP_NONE, so components are stored. */
        member.compression = XX_UIMAGE_COMP_NONE;
        if (!member.name || !xx_uimage_append_member(parsed, &member)) {
            if (member.name) xx_str_free(member.name);
            return false;
        }
        component += (int64_t)((((uint64_t)sizes[index]) + 3U) &
                               ~(uint64_t)3U);
    }
    return true;
}

static bool xx_uimage_parse(Abstractformat *self, xx_uimage_private *parsed,
                            xx_pd_struct *pd) {
    uint8_t header[XX_UIMAGE_HEADER_SIZE];
    uint8_t zeroed[XX_UIMAGE_HEADER_SIZE];
    uint32_t computed;
    uint32_t payload_crc = 0U;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->data_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_uimage_range_within(parsed->input_size, self->base_address,
                                XX_UIMAGE_HEADER_SIZE) ||
        !xx_uimage_read_at(self->device, self->base_address, header,
                           sizeof(header)) ||
        xx_data_get_u32(header, sizeof(header), 0U, true) != XX_UIMAGE_MAGIC) {
        goto fail;
    }
    parsed->header_crc = xx_data_get_u32(header, sizeof(header), 4U, true);
    parsed->timestamp = xx_data_get_u32(header, sizeof(header), 8U, true);
    parsed->data_size = xx_data_get_u32(header, sizeof(header), 12U, true);
    parsed->load_address = xx_data_get_u32(header, sizeof(header), 16U, true);
    parsed->entry_point = xx_data_get_u32(header, sizeof(header), 20U, true);
    parsed->data_crc = xx_data_get_u32(header, sizeof(header), 24U, true);
    parsed->os = xx_data_get_u8(header, sizeof(header), 28U);
    parsed->cpu_arch = xx_data_get_u8(header, sizeof(header), 29U);
    parsed->image_type = xx_data_get_u8(header, sizeof(header), 30U);
    parsed->compression = xx_data_get_u8(header, sizeof(header), 31U);
    xx_rt_memcpy(parsed->name, header + 32, XX_UIMAGE_NAME_SIZE);
    parsed->name[XX_UIMAGE_NAME_SIZE] = '\0';

    /* The header CRC covers all 64 bytes with ih_hcrc read as zero.  Getting
     * this wrong is the classic uImage bug, so the zeroing is done on an
     * explicit copy rather than by subtracting the field's contribution. */
    xx_rt_memcpy(zeroed, header, sizeof(header));
    xx_rt_memset(zeroed + XX_UIMAGE_HCRC_OFFSET, 0, 4U);
    computed = xx_crc32_calc(0U, zeroed, sizeof(zeroed));
    if (computed != parsed->header_crc) goto fail;

    if (!xx_uimage_add(self->base_address, XX_UIMAGE_HEADER_SIZE,
                       &parsed->data_offset) ||
        !xx_uimage_range_within(parsed->input_size, parsed->data_offset,
                                (int64_t)parsed->data_size) ||
        !xx_uimage_add(parsed->data_offset, parsed->data_size,
                       &parsed->archive_end)) {
        goto fail;
    }
    /* ih_dcrc is not advisory: U-Boot refuses to boot an image whose payload
     * checksum does not match, so a mismatch is a parse failure here too. */
    if (!xx_uimage_crc_range(self->device, parsed->data_offset,
                             (int64_t)parsed->data_size, &payload_crc, pd) ||
        payload_crc != parsed->data_crc) {
        goto fail;
    }

    if (parsed->image_type == XX_UIMAGE_TYPE_MULTI &&
        parsed->compression == XX_UIMAGE_COMP_NONE) {
        if (!xx_uimage_parse_multi_members(self, parsed)) goto fail;
    } else {
        xx_uimage_member member;
        xx_mem_zero(&member, sizeof(member));
        member.name = xx_uimage_member_name(parsed->name, "uimage.bin");
        member.data_offset = parsed->data_offset;
        member.data_size = (int64_t)parsed->data_size;
        member.compression = parsed->compression;
        if (!member.name || !xx_uimage_append_member(parsed, &member)) {
            if (member.name) xx_str_free(member.name);
            goto fail;
        }
    }
    return true;
fail:
    xx_uimage_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Decompression                                                             */
/* ------------------------------------------------------------------------ */

const char *xx_uimage_compression_to_string(uint8_t compression) {
    switch (compression) {
        case XX_UIMAGE_COMP_NONE: return "none";
        case XX_UIMAGE_COMP_GZIP: return "gzip";
        case XX_UIMAGE_COMP_BZIP2: return "bzip2";
        case XX_UIMAGE_COMP_LZMA: return "lzma";
        case XX_UIMAGE_COMP_LZO: return "lzo";
        case XX_UIMAGE_COMP_LZ4: return "lz4";
        case XX_UIMAGE_COMP_ZSTD: return "zstd";
        default: return "unknown";
    }
}

/*
 * Write one member's bytes to destination, decoding ih_comp on the way.
 *
 * LZ4 is a deliberate gap.  The only public framed-LZ4 entry point in this
 * library, xx_lz4_decompress_memory(), wants an EXACT output size, and a
 * uImage header does not publish the decompressed length.  An IH_COMP_LZ4
 * payload is therefore emitted as stored bytes rather than guessed at; the
 * caller still gets the frame and can hand it to the lz4 reader.
 */
static bool xx_uimage_emit_member(Abstractformat *self,
                                  const xx_uimage_member *member,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    if (!self || !self->device || !member || !destination) return false;
    switch (member->compression) {
        case XX_UIMAGE_COMP_GZIP: {
            /* The payload is a complete gzip member, so the gz reader is
             * pointed straight at it.  It stops at the member's own trailer;
             * the declared ih_size bounds how far it can run. */
            xx_gz gz;
            bool result;
            xx_gz_init(&gz, self->device, member->data_offset);
            result = xx_gz_unpack_to_device(&gz, destination, pd);
            xx_gz_destroy(&gz);
            return result;
        }
        case XX_UIMAGE_COMP_BZIP2:
            return xx_bzip2_unpack_device(self->device, member->data_offset,
                                          member->data_size, destination, pd);
        case XX_UIMAGE_COMP_LZMA:
            /* mkimage -C lzma emits an LZMA-Alone stream: 13-byte header,
             * then the range coded data with an end marker. */
            return xx_lzma_alone_decode_device(self->device,
                                               member->data_offset,
                                               member->data_size, destination,
                                               NULL, pd);
        case XX_UIMAGE_COMP_LZO:
            /* U-Boot's IH_COMP_LZO payload is a whole lzop file, header and
             * all, not a bare LZO1X block. */
            return xx_lzop_decode_device(self->device, member->data_offset,
                                         member->data_size, destination, NULL,
                                         NULL, pd);
        case XX_UIMAGE_COMP_ZSTD:
            /* Zero means "size not declared"; a uImage header carries no
             * decompressed length to pass along. */
            return xx_zstd_unpack_device_to_device(
                self->device, member->data_offset, member->data_size,
                destination, 0U, pd);
        case XX_UIMAGE_COMP_LZ4:
        case XX_UIMAGE_COMP_NONE:
        default:
            /* Stored, plus the LZ4 gap described above and any ih_comp this
             * reader does not know: the bytes go out untouched. */
            return xx_store_unpack_device(self->device, member->data_offset,
                                          member->data_size, destination, pd);
    }
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_uimage_copy_options(xx_list_s *destination,
                                   const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_uimage_find_option(const xx_list_s *options,
                                           uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_uimage_populate_record(xx_archive_record *record,
                                      const xx_uimage_private *parsed,
                                      const xx_uimage_member *member) {
    if (!record || !parsed || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->data_offset - XX_UIMAGE_HEADER_SIZE;
    record->header_size = XX_UIMAGE_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           /* A compressed payload publishes no decompressed length, so the
            * uncompressed size is only meaningful when the member is
            * stored; it is reported as zero otherwise. */
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               member->compression == XX_UIMAGE_COMP_NONE
                   ? (uint64_t)member->data_size
                   : 0U) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          member->compression) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          parsed->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_uimage_archive_stream_free(void *pointer) {
    xx_uimage_archive_stream *stream = (xx_uimage_archive_stream *)pointer;
    if (!stream) return;
    xx_uimage_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_uimage_init(xx_uimage *uimage, xx_io_device *dev,
                    int64_t base_address) {
    if (!uimage) return;
    xx_mem_zero(uimage, sizeof(*uimage));
    xx_format_init(&uimage->format, dev, base_address);
    uimage->format.endian = XX_ENDIAN_BIG;
    uimage->format.file_type = XX_UIMAGE_FILE_TYPE;
    uimage->format.format_type = XX_TYPE_ARCHIVE;
    uimage->format.is_archive = true;
    xx_format_set_mime_type(&uimage->format, "application/x-uimage");
    xx_format_set_extension(&uimage->format, "uimg");
    uimage->format.check_is_valid = xx_uimage_check_is_valid;
    uimage->format.handle_base_info = xx_uimage_handle_base_info;
    uimage->format.get_format_size = xx_uimage_get_format_size;
    uimage->format.get_number_of_archive_records =
        xx_uimage_get_number_of_archive_records;
    uimage->format.create_archive_records_reading =
        xx_uimage_create_archive_records_reading;
    uimage->format.get_current_archive_record =
        xx_uimage_get_current_archive_record;
    uimage->format.unpack_current_archive_record =
        xx_uimage_unpack_current_archive_record;
    uimage->format.archive_record_move_to_next =
        xx_uimage_archive_record_move_to_next;
    uimage->format.free_archive_records_reading =
        xx_uimage_free_archive_records_reading;
    uimage->format.destroy = xx_uimage_vtable_destroy;
    uimage->archive_end = -1;
}

xx_uimage *xx_uimage_create(xx_io_device *dev, int64_t base_address) {
    xx_uimage *uimage = (xx_uimage *)xx_mem_alloc(sizeof(*uimage));
    if (uimage) xx_uimage_init(uimage, dev, base_address);
    return uimage;
}

void xx_uimage_destroy(xx_uimage *uimage) {
    if (!uimage) return;
    if (uimage->internal) {
        xx_uimage_private_cleanup((xx_uimage_private *)uimage->internal);
        xx_mem_free(uimage->internal);
        uimage->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&uimage->format);
}

static void xx_uimage_vtable_destroy(Abstractformat *self) {
    xx_uimage_destroy((xx_uimage *)self);
}

void xx_uimage_free(xx_uimage *uimage) {
    if (!uimage) return;
    xx_uimage_destroy(uimage);
    xx_mem_free(uimage);
}

bool xx_uimage_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_uimage_private parsed;
    bool result = xx_uimage_parse(self, &parsed, pd);
    xx_uimage_private_cleanup(&parsed);
    return result;
}

bool xx_uimage_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_uimage_private *parsed;
    xx_uimage *uimage = (xx_uimage *)self;
    int64_t total_size;
    if (!self || !uimage) return false;
    parsed = (xx_uimage_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_uimage_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (uimage->internal) {
        xx_uimage_private_cleanup((xx_uimage_private *)uimage->internal);
        xx_mem_free(uimage->internal);
    }
    uimage->internal = parsed;
    uimage->number_of_records = parsed->count;
    uimage->number_of_members = parsed->count;
    uimage->header_crc = parsed->header_crc;
    uimage->data_crc = parsed->data_crc;
    uimage->timestamp = parsed->timestamp;
    uimage->data_size = parsed->data_size;
    uimage->load_address = parsed->load_address;
    uimage->entry_point = parsed->entry_point;
    uimage->os = parsed->os;
    uimage->cpu_arch = parsed->cpu_arch;
    uimage->image_type = parsed->image_type;
    uimage->compression = parsed->compression;
    xx_rt_memcpy(uimage->name, parsed->name, sizeof(uimage->name));
    uimage->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_uimage_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_uimage_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_uimage *)self)->number_of_records;
}

xx_archive_record_state *xx_uimage_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_uimage_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_uimage_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_uimage_copy_options(&state->options, options) ||
        !xx_uimage_parse(self, &stream->parsed, pd)) {
        xx_uimage_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_uimage_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_uimage_populate_record(&state->current_record, &stream->parsed,
                                  &stream->parsed.members[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_uimage_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_uimage_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_uimage_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_uimage_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_uimage_populate_record(&state->current_record, &stream->parsed,
                                   &stream->parsed.members[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_uimage_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_uimage_archive_stream *stream;
    const xx_uimage_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_uimage_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    member = &stream->parsed.members[stream->index];
    if (!xx_uimage_safe_name(member->name)) return false;

    option = xx_uimage_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the member's span is addressable. */
        return member->data_offset >= 0 && member->data_size >= 0 &&
               member->data_offset <= stream->parsed.input_size &&
               member->data_size <=
                   stream->parsed.input_size - member->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination_path = xx_str_concat3(base, "/", member->name);
    } else {
        destination_path = xx_str_concat(base, member->name);
    }
    if (!destination_path) goto cleanup;
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    created = destination != NULL;
    if (!destination) goto cleanup;
    result = xx_uimage_emit_member(self, member, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_uimage_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_uimage_get_number_of_records(const xx_uimage *uimage) {
    return uimage ? uimage->number_of_records : 0U;
}
uint64_t xx_uimage_get_number_of_members(const xx_uimage *uimage) {
    return uimage ? uimage->number_of_members : 0U;
}
uint32_t xx_uimage_get_data_size(const xx_uimage *uimage) {
    return uimage ? uimage->data_size : 0U;
}
uint32_t xx_uimage_get_data_crc(const xx_uimage *uimage) {
    return uimage ? uimage->data_crc : 0U;
}
uint32_t xx_uimage_get_header_crc(const xx_uimage *uimage) {
    return uimage ? uimage->header_crc : 0U;
}
uint8_t xx_uimage_get_compression(const xx_uimage *uimage) {
    return uimage ? uimage->compression : 0U;
}
uint8_t xx_uimage_get_image_type(const xx_uimage *uimage) {
    return uimage ? uimage->image_type : 0U;
}
const char *xx_uimage_get_name(const xx_uimage *uimage) {
    return uimage ? uimage->name : "";
}
int64_t xx_uimage_get_archive_end(const xx_uimage *uimage) {
    return uimage ? uimage->archive_end : -1;
}
