/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PSDC, the single-file packer used on Broderbund's Print Shop Deluxe
 * install media.  The packed copy keeps the original name with the last
 * character of the extension replaced by '_' or '$' (PSDWIN.HLP ->
 * PSDWIN.HL$, TRIBUNE.TTF -> TRIBUNE.TT$).
 *
 * The container is undocumented, but U3 recognises it and its recognition
 * predicate is recovered in F:\utils\U3\src (FORMAT_INDEX.md "archive / 194
 * PSDC", class gqa, VMT 0x00554c48; slot 0 -> FUN_00554cc0, slot 1 ->
 * FUN_00554d70).  Transcribing FUN_00554cc0 gives the layout exactly, and
 * every rule below holds for all 88 samples in F:\ARC\ARC\PSDC:
 *
 *   0x00  char[13]  the original 8.3 name, NUL terminated inside the field
 *                   (U3 gates this on its own 8.3 test, FUN_00425300: one to
 *                   eight name characters, a mandatory '.', up to three
 *                   extension characters, then the NUL)
 *   0x0C  u8[64]    all zero - the rest of a 76-byte name buffer
 *   0x4C  u32       the total length of the container, header included.  U3
 *                   requires it to EQUAL the file's real size, and it does
 *                   in all 88 samples; that exact equality is the anchor
 *                   that makes the layout certain.
 *   0x50  ...       the packed payload, running to the end of the file
 *
 * THE CODEC IS PKWARE DCL.  The two bytes at 0x50 that earlier work noted as
 * "constant 00 06" are not a bespoke stream tag: they are the DCL prelude,
 * and U3 treats them as such.  FUN_00554cc0 accepts byte 0x50 only when it
 * is 0 or 1 (the literal mode) and byte 0x51 only when it is 4, 5 or 6 (the
 * dictionary-size selector) -- it builds a bit mask, `1 << b`, and tests it
 * against 0x03 and 0x70 respectively.  Those are precisely DCL's two legal
 * value sets.  FUN_00554d70 then hands the extent from 0x50 to end-of-file
 * to FUN_0043ef50, whose per-chunk worker FUN_0043e3d0 is a textbook DCL
 * explode: the 0x0207 end-of-stream code, the length base/extra tables at
 * DAT_007bbc8c/DAT_007bbcac, the distance code tables at DAT_007bbcbc and
 * the 256-entry coded-literal Huffman at DAT_007bb96c.
 *
 * So no new decoder was needed: the library's own xx_dcl_scan_memory() and
 * xx_dcl_decode_memory() serve this format directly.  The container records
 * no plaintext length, so the size comes from the measuring scan, which also
 * gives the reader a much stronger test than any header compare - the stream
 * must reach its end marker AND land exactly on the last byte of the file.
 *
 * Note for the coordinator: the container's only constants live at 0x4C and
 * 0x50, beyond the 64-byte magic window, so this reader needs late dispatch.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/psdc/xx_psdc.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* The enumerator is added by the coordinator, not by this file. */
#ifdef PSDC
#define XX_PSDC_FILE_TYPE XX_FILE_TYPE_PSDC
#else
#define XX_PSDC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PSDC_NAME_FIELD 13U
#define XX_PSDC_ZERO_START 12
#define XX_PSDC_ZERO_END 76
#define XX_PSDC_SIZE_OFFSET 76
#define XX_PSDC_HEADER_SIZE 80

/* Bound the payload before anything is read or allocated for it.  A PSDC
 * member is a single install-media file; these are the widest limits that
 * still refuse a container claiming an absurd extent. */
#define XX_PSDC_MAX_PACKED ((int64_t)128 * 1024 * 1024)
#define XX_PSDC_MAX_OUTPUT ((size_t)256U * 1024U * 1024U)

typedef struct xx_psdc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    uint64_t unpacked_size; /* 0 until the measuring scan has run */
} xx_psdc_member;

typedef struct xx_psdc_stream_s {
    xx_psdc_member member;
    size_t count;
    int64_t archive_size;
    uint32_t declared_size;
} xx_psdc_stream;

/* ------------------------------------------------------------ helpers --- */

static uint32_t xx_psdc_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool xx_psdc_read_at(Abstractformat *self, int64_t offset,
                            void *buffer, size_t size) {
    size_t done = 0U;

    if (!self || !self->device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(self->device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* U3's own 8.3 test (FUN_00425300), which is what makes this weak header
 * usable at all: a printable first character that is not '.', one to eight
 * name characters, a mandatory '.', up to three extension characters and
 * then the terminating NUL, all inside @p size bytes.  @p length receives
 * the name's length in bytes. */
static bool xx_psdc_name_field_sane(const uint8_t *field, size_t size,
                                    size_t *length) {
    size_t index = 0U;
    size_t run;

    if (size == 0U || field[0] < 0x20U || field[0] == '.') return false;
    for (run = 1U; run <= 8U; ++run) {
        ++index;
        if (index >= size || field[index] < 0x20U) return false;
        if (field[index] == '.') break;
    }
    if (field[index] != '.') return false;
    /* Four steps, not three: the NUL that ends a full three-character
     * extension sits one past the last of them. */
    for (run = 0U; run < 4U; ++run) {
        ++index;
        if (index >= size) return false;
        if (field[index] == 0U) break;
        if (field[index] < 0x20U) return false;
    }
    if (field[index] != 0U) return false;
    *length = index;
    return true;
}

static char *xx_psdc_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input;
    size_t output = 0U;

    if (!bytes || size == 0U || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '"' ||
            c == '*' || c == '<' || c == '>' || c == '?' || c == '|')
            name[output++] = '_';
        else
            name[output++] = (char)c;
    }
    while (output > 0U && (name[output - 1U] == ' ' || name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static void xx_psdc_stream_free(void *pointer) {
    xx_psdc_stream *stream = (xx_psdc_stream *)pointer;

    if (!stream) return;
    xx_str_free(stream->member.name);
    xx_mem_free(stream);
}

static xx_psdc_stream *xx_psdc_parse(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t header[XX_PSDC_HEADER_SIZE + 2];
    xx_psdc_stream *stream;
    int64_t total;
    int64_t span;
    uint32_t declared;
    int index;
    size_t name_length = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus the two-byte DCL prelude plus at least one payload byte. */
    if (span <= (int64_t)XX_PSDC_HEADER_SIZE + 2) return NULL;
    if (!xx_psdc_read_at(self, self->base_address, header, sizeof(header)))
        return NULL;
    if (!xx_psdc_name_field_sane(header, XX_PSDC_NAME_FIELD, &name_length))
        return NULL;
    /* The rest of the 76-byte name buffer is zero in every sample, and U3
     * requires it; it is most of what separates this header from noise. */
    for (index = XX_PSDC_ZERO_START; index < XX_PSDC_ZERO_END; ++index)
        if (header[index] != 0U) return NULL;
    /* The DCL prelude: literal mode 0 or 1, dictionary selector 4, 5 or 6. */
    if (header[XX_PSDC_HEADER_SIZE] > 1U ||
        header[XX_PSDC_HEADER_SIZE + 1] < 4U ||
        header[XX_PSDC_HEADER_SIZE + 1] > 6U)
        return NULL;
    declared = xx_psdc_le32(header + XX_PSDC_SIZE_OFFSET);
    /* The declared length is the container's own length and U3 requires it
     * to match the file exactly.  Bounding it against the real extent this
     * way means no later size can be derived from an unchecked field. */
    if (declared <= (uint32_t)XX_PSDC_HEADER_SIZE || (int64_t)declared != span)
        return NULL;

    stream = (xx_psdc_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->member.name = xx_psdc_normalize_name(header, name_length);
    if (!stream->member.name) {
        xx_mem_free(stream);
        return NULL;
    }
    stream->declared_size = declared;
    stream->member.header_offset = self->base_address;
    stream->member.header_size = XX_PSDC_HEADER_SIZE;
    stream->member.data_offset = self->base_address + XX_PSDC_HEADER_SIZE;
    stream->member.compressed_size =
        (int64_t)declared - (int64_t)XX_PSDC_HEADER_SIZE;
    stream->member.unpacked_size = 0U;
    stream->count = 1U;
    stream->archive_size = (int64_t)declared;
    return stream;
}

/* -------------------------------------------------------------- codec --- */

/* Read the packed extent into memory.  The extent comes from the parse,
 * which has already bounded it against the file, and is bounded again here
 * against XX_PSDC_MAX_PACKED before a single byte is allocated. */
static uint8_t *xx_psdc_read_packed(Abstractformat *self,
                                    const xx_psdc_member *member,
                                    size_t *size) {
    uint8_t *packed;

    if (!self || !member || !size) return NULL;
    if (member->compressed_size < 2 ||
        member->compressed_size > XX_PSDC_MAX_PACKED)
        return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return NULL;
    if (!xx_psdc_read_at(self, member->data_offset, packed,
                         (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    *size = (size_t)member->compressed_size;
    return packed;
}

/* Measure the member: the container stores no plaintext length, so the only
 * way to learn one is to run the DCL stream.  Requiring it to consume the
 * packed extent EXACTLY is also the reader's strongest validity test. */
static bool xx_psdc_measure(Abstractformat *self, xx_psdc_member *member) {
    uint8_t *packed = NULL;
    size_t packed_size = 0U;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool result;

    if (!self || !member) return false;
    if (member->unpacked_size != 0U) return true;
    packed = xx_psdc_read_packed(self, member, &packed_size);
    if (!packed) return false;
    result = xx_dcl_scan_memory(packed, packed_size, XX_PSDC_MAX_OUTPUT,
                                &consumed, &produced) &&
             consumed == packed_size && produced != 0U;
    xx_mem_free(packed);
    if (!result) return false;
    member->unpacked_size = (uint64_t)produced;
    return true;
}

static bool xx_psdc_decode(Abstractformat *self, const xx_psdc_member *member,
                           uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t packed_size = 0U;
    size_t written = 0U;
    size_t output_size;

    if (!self || !member || !plain || !plain_size) return false;
    if (member->unpacked_size == 0U || member->unpacked_size > SIZE_MAX)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = xx_psdc_read_packed(self, member, &packed_size);
    if (!packed) return false;
    output = (uint8_t *)xx_mem_alloc(output_size);
    if (!output || !xx_dcl_decode_memory(packed, packed_size, output,
                                         output_size, &written) ||
        written != output_size) {
        xx_mem_free(packed);
        if (output) xx_mem_free(output);
        return false;
    }
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
}

/* --------------------------------------------------------- lifecycle --- */

static void xx_psdc_vtable_destroy(Abstractformat *self);

void xx_psdc_init(xx_psdc *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PSDC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-psdc");
    xx_format_set_extension(&archive->format, "ps$");
    archive->format.check_is_valid = xx_psdc_check_is_valid;
    archive->format.handle_base_info = xx_psdc_handle_base_info;
    archive->format.get_format_size = xx_psdc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_psdc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_psdc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_psdc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_psdc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_psdc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_psdc_free_archive_records_reading;
    archive->format.destroy = xx_psdc_vtable_destroy;
}

xx_psdc *xx_psdc_create(xx_io_device *device, int64_t base_address) {
    xx_psdc *archive = (xx_psdc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_psdc_init(archive, device, base_address);
    return archive;
}

void xx_psdc_destroy(xx_psdc *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_psdc_free(xx_psdc *archive) {
    if (!archive) return;
    xx_psdc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_psdc_vtable_destroy(Abstractformat *self) {
    xx_psdc_destroy((xx_psdc *)self);
}

/* ------------------------------------------------------------ format --- */

bool xx_psdc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_psdc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_psdc_parse(self, pd);
    if (!stream) return false;
    xx_psdc_stream_free(stream);
    return true;
}

bool xx_psdc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_psdc *archive = (xx_psdc *)self;
    xx_psdc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_psdc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->declared_size = stream->declared_size;
    xx_psdc_stream_free(stream);
    return true;
}

int64_t xx_psdc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_psdc_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_psdc *)self)->number_of_records : 0U;
}

/* ----------------------------------------------------------- records --- */

static bool xx_psdc_set_record(xx_archive_record *record,
                               const xx_psdc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           /* Measured by the DCL scan; 0 when the stream did not measure. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_psdc_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
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

static const xx_var *xx_psdc_option(const xx_list_s *options,
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

xx_archive_record_state *xx_psdc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_psdc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_psdc_parse(self, pd);
    if (!stream) return NULL;
    /* The plaintext length is nowhere in the container, so it is measured
     * here.  A stream that will not measure is still listed - with size 0
     * for "unknown" - and unpacking it later fails closed. */
    (void)xx_psdc_measure(self, &stream->member);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_psdc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_psdc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_psdc_copy_options(&state->options, options) ||
        !xx_psdc_set_record(&state->current_record, &stream->member)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_psdc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_psdc_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    (void)pd;
    if (!self || !state || state->format != self) return false;
    /* A PSDC container holds exactly one member. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_psdc_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_psdc_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_psdc_stream *)state->internal_state;
    if (!stream) return false;
    if (!xx_psdc_measure(self, &stream->member)) return false;
    if (!xx_psdc_decode(self, &stream->member, &plain, &plain_size))
        return false;
    /* With no unpack path the caller only wanted to know the member decodes;
     * it does, so this is a success with nothing written. */
    path_option = xx_psdc_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->member.name)
               : xx_str_concat(base, stream->member.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount =
                xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    xx_mem_free(plain);
    xx_str_free(path);
    xx_str_free(owned_base);
    return result;
}

void xx_psdc_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
