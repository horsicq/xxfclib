/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mh01/xx_mh01.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_MH01 exists in the enum. */
#ifdef MH01
#define XX_MH01_FILE_TYPE XX_FILE_TYPE_MH01
#else
#define XX_MH01_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** OpenSSL's "enc" container prefix, expected at the start of the payload. */
#define XX_MH01_OPENSSL_MAGIC "Salted__"
#define XX_MH01_OPENSSL_MAGIC_SIZE 8U
/** "Salted__" plus the eight-byte salt: the least an "enc" container holds. */
#define XX_MH01_OPENSSL_HEADER_SIZE 16U

typedef struct xx_mh01_region_s {
    const char *name; /**< A literal chosen here, never from the file. */
    int64_t data_offset;
    int64_t data_size;
    bool is_encrypted;
} xx_mh01_region;

typedef struct xx_mh01_private_s {
    xx_mh01_region regions[XX_MH01_MAX_RECORDS];
    size_t count;
    int64_t input_size;
    int64_t archive_end;
    int64_t iv_offset;
    int64_t encrypted_data_offset;
    int64_t signature_data_offset;
    uint32_t signature_offset;
    uint32_t signature_size;
    uint32_t iv_size;
    uint32_t encrypted_data_size;
    uint32_t unknown1;
    uint32_t unknown2;
    char *iv;
} xx_mh01_private;

typedef struct xx_mh01_archive_stream_s {
    xx_mh01_private parsed;
    size_t index;
} xx_mh01_archive_stream;

static void xx_mh01_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: the header fields are 32-bit but the
 * base address inside a larger carrier is not, and long is 32-bit on Win64. */
static bool xx_mh01_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_mh01_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_mh01_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_mh01_private_cleanup(xx_mh01_private *parsed) {
    if (!parsed) return;
    if (parsed->iv) xx_str_free(parsed->iv);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->iv_offset = -1;
    parsed->encrypted_data_offset = -1;
    parsed->signature_data_offset = -1;
}

static void xx_mh01_push(xx_mh01_private *parsed, const char *name,
                         int64_t offset, int64_t size, bool is_encrypted) {
    xx_mh01_region *region;
    if (!parsed || parsed->count >= XX_MH01_MAX_RECORDS || size <= 0) return;
    region = &parsed->regions[parsed->count++];
    region->name = name;
    region->data_offset = offset;
    region->data_size = size;
    region->is_encrypted = is_encrypted;
}

static bool xx_mh01_parse(Abstractformat *self, xx_mh01_private *parsed,
                          xx_pd_struct *pd) {
    uint8_t header[XX_MH01_HEADER_SIZE];
    uint8_t openssl_magic[XX_MH01_OPENSSL_MAGIC_SIZE];
    uint64_t total_span;
    int64_t payload_end = -1;
    uint32_t index;
    uint32_t hex_digits;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
        parsed->iv_offset = -1;
        parsed->encrypted_data_offset = -1;
        parsed->signature_data_offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_mh01_range_within(parsed->input_size, self->base_address,
                              XX_MH01_HEADER_SIZE) ||
        !xx_mh01_read_at(self->device, self->base_address, header,
                         XX_MH01_HEADER_SIZE)) {
        goto fail;
    }
    /* Both sub-headers carry the magic.  Requiring the second one is what
     * keeps a stray "MH01" in the middle of some other file from parsing. */
    if (xx_data_get_u32(header, sizeof(header), 0U, false) != XX_MH01_MAGIC ||
        xx_data_get_u32(header, sizeof(header), 16U, false) != XX_MH01_MAGIC) {
        goto fail;
    }
    parsed->signature_offset = xx_data_get_u32(header, sizeof(header), 4U,
                                               false);
    parsed->signature_size = xx_data_get_u32(header, sizeof(header), 8U, false);
    parsed->unknown1 = xx_data_get_u32(header, sizeof(header), 12U, false);
    parsed->iv_size = xx_data_get_u32(header, sizeof(header), 20U, false);
    parsed->encrypted_data_size =
        xx_data_get_u32(header, sizeof(header), 24U, false);
    parsed->unknown2 = xx_data_get_u32(header, sizeof(header), 28U, false);

    /* The IV is buffered whole, so it is capped independently of the device
     * size; an AES-128 IV is 32 hex characters and nothing legitimate is
     * anywhere near the limit.  The payload is an OpenSSL "enc" container,
     * whose own header ("Salted__" plus an eight-byte salt) is sixteen bytes,
     * so anything shorter cannot be one. */
    if (parsed->iv_size == 0U || parsed->iv_size > XX_MH01_MAX_IV_SIZE ||
        parsed->encrypted_data_size < XX_MH01_OPENSSL_HEADER_SIZE ||
        parsed->signature_size == 0U) {
        goto fail;
    }

    parsed->iv_offset = self->base_address + (int64_t)XX_MH01_HEADER_SIZE;
    if (!xx_mh01_add(parsed->iv_offset, parsed->iv_size,
                     &parsed->encrypted_data_offset) ||
        !xx_mh01_add(self->base_address,
                     (uint64_t)XX_MH01_SUBHEADER_SIZE +
                         (uint64_t)parsed->signature_offset,
                     &parsed->signature_data_offset)) {
        goto fail;
    }

    /* Every declared region has to be physically present.  These three
     * lengths are straight out of the file and are the expansion hazard in
     * this format: bound them here, at parse, not at extraction. */
    if (!xx_mh01_range_within(parsed->input_size, parsed->iv_offset,
                              (int64_t)parsed->iv_size) ||
        !xx_mh01_range_within(parsed->input_size, parsed->encrypted_data_offset,
                              (int64_t)parsed->encrypted_data_size) ||
        !xx_mh01_range_within(parsed->input_size, parsed->signature_data_offset,
                              (int64_t)parsed->signature_size)) {
        goto fail;
    }

    /* The IV is stored as ASCII hex with no NUL terminator, but on genuine
     * images it is NOT pure hex: the field is what `openssl rand -hex 16`
     * prints, 32 hex digits and a newline, so iv_size is 33 and the payload
     * starts at 0x41 (delink hard-codes exactly that; binwalk trim()s the
     * field).  Accept a non-empty run of hex digits followed only by ASCII
     * whitespace, and keep the trimmed digits as the IV.  Anything else -
     * a NUL, a non-hex byte, whitespace before or between digits - means
     * the layout was guessed wrong, so it is a parse failure. */
    parsed->iv = xx_str_create_len(parsed->iv_size);
    if (!parsed->iv ||
        !xx_mh01_read_at(self->device, parsed->iv_offset, parsed->iv,
                         parsed->iv_size)) {
        goto fail;
    }
    parsed->iv[parsed->iv_size] = '\0';
    hex_digits = 0U;
    for (index = 0U; index < parsed->iv_size; ++index) {
        char c = parsed->iv[index];
        bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F');
        bool is_space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (is_hex && hex_digits == index) {
            ++hex_digits;
        } else if (!is_space || hex_digits == 0U) {
            goto fail;
        }
    }
    parsed->iv[hex_digits] = '\0';

    /* binwalk's signature module requires the payload to parse as an OpenSSL
     * "enc" container, and so does this reader: without it "MH01" plus four
     * plausible lengths is not enough evidence.  encrypted_data_size >= 16
     * was checked above and the region is inside the file, so the magic read
     * stays inside the payload. */
    if (!xx_mh01_read_at(self->device, parsed->encrypted_data_offset,
                         openssl_magic, XX_MH01_OPENSSL_MAGIC_SIZE) ||
        xx_rt_memcmp(openssl_magic, XX_MH01_OPENSSL_MAGIC,
                     XX_MH01_OPENSSL_MAGIC_SIZE) != 0) {
        goto fail;
    }

    /* The signature is detached and trails the image: it may follow after a
     * gap, but it may not start inside the header, the IV or the payload it
     * signs.  With that ordering the end of the signature is the end of the
     * whole image. */
    if (!xx_mh01_add(parsed->encrypted_data_offset,
                     parsed->encrypted_data_size, &payload_end) ||
        parsed->signature_data_offset < payload_end) {
        goto fail;
    }
    total_span = (uint64_t)XX_MH01_SUBHEADER_SIZE +
                 (uint64_t)parsed->signature_offset +
                 (uint64_t)parsed->signature_size;
    if (!xx_mh01_add(self->base_address, total_span, &parsed->archive_end) ||
        parsed->archive_end > parsed->input_size) {
        goto fail;
    }

    xx_mh01_push(parsed, "iv.bin", parsed->iv_offset, (int64_t)parsed->iv_size,
                 false);
    xx_mh01_push(parsed, "encrypted.bin", parsed->encrypted_data_offset,
                 (int64_t)parsed->encrypted_data_size, true);
    xx_mh01_push(parsed, "signature.bin", parsed->signature_data_offset,
                 (int64_t)parsed->signature_size, false);
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_mh01_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_mh01_copy_options(xx_list_s *destination,
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

static const xx_var *xx_mh01_find_option(const xx_list_s *options,
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

static bool xx_mh01_populate_record(xx_archive_record *record,
                                    const xx_mh01_private *parsed,
                                    const xx_mh01_region *region) {
    if (!record || !parsed || !region || !region->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->iv_offset - (int64_t)XX_MH01_HEADER_SIZE;
    record->header_size = (int64_t)XX_MH01_HEADER_SIZE;
    record->data_offset = region->data_offset;
    record->compressed_size = region->data_size;
    /* Nothing inside an MH01 image is compressed, so the two sizes agree and
     * the compression method is "none".  The payload is encrypted with a key
     * this library does not have, which is what the flag records. */
    return xx_archive_record_set_original_name(record, region->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)region->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)region->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           region->is_encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_mh01_archive_stream_free(void *pointer) {
    xx_mh01_archive_stream *stream = (xx_mh01_archive_stream *)pointer;
    if (!stream) return;
    xx_mh01_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_mh01_init(xx_mh01 *mh01, xx_io_device *dev, int64_t base_address) {
    if (!mh01) return;
    xx_mem_zero(mh01, sizeof(*mh01));
    xx_format_init(&mh01->format, dev, base_address);
    mh01->format.endian = XX_ENDIAN_LITTLE;
    mh01->format.file_type = XX_MH01_FILE_TYPE;
    mh01->format.format_type = XX_TYPE_ARCHIVE;
    mh01->format.is_archive = true;
    mh01->format.is_crypted = true;
    mh01->format.is_signed = true;
    xx_format_set_mime_type(&mh01->format, "application/x-dlink-mh01");
    xx_format_set_extension(&mh01->format, "bin");
    mh01->format.check_is_valid = xx_mh01_check_is_valid;
    mh01->format.handle_base_info = xx_mh01_handle_base_info;
    mh01->format.get_format_size = xx_mh01_get_format_size;
    mh01->format.get_number_of_archive_records =
        xx_mh01_get_number_of_archive_records;
    mh01->format.create_archive_records_reading =
        xx_mh01_create_archive_records_reading;
    mh01->format.get_current_archive_record =
        xx_mh01_get_current_archive_record;
    mh01->format.unpack_current_archive_record =
        xx_mh01_unpack_current_archive_record;
    mh01->format.archive_record_move_to_next =
        xx_mh01_archive_record_move_to_next;
    mh01->format.free_archive_records_reading =
        xx_mh01_free_archive_records_reading;
    mh01->format.destroy = xx_mh01_vtable_destroy;
    mh01->iv_offset = -1;
    mh01->encrypted_data_offset = -1;
    mh01->signature_data_offset = -1;
    mh01->archive_end = -1;
}

xx_mh01 *xx_mh01_create(xx_io_device *dev, int64_t base_address) {
    xx_mh01 *mh01 = (xx_mh01 *)xx_mem_alloc(sizeof(*mh01));
    if (mh01) xx_mh01_init(mh01, dev, base_address);
    return mh01;
}

void xx_mh01_destroy(xx_mh01 *mh01) {
    if (!mh01) return;
    if (mh01->internal) {
        xx_mh01_private_cleanup((xx_mh01_private *)mh01->internal);
        xx_mem_free(mh01->internal);
        mh01->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&mh01->format);
}

static void xx_mh01_vtable_destroy(Abstractformat *self) {
    xx_mh01_destroy((xx_mh01 *)self);
}

void xx_mh01_free(xx_mh01 *mh01) {
    if (!mh01) return;
    xx_mh01_destroy(mh01);
    xx_mem_free(mh01);
}

bool xx_mh01_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mh01_private parsed;
    bool result = xx_mh01_parse(self, &parsed, pd);
    xx_mh01_private_cleanup(&parsed);
    return result;
}

bool xx_mh01_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mh01_private *parsed;
    xx_mh01 *mh01 = (xx_mh01 *)self;
    int64_t total_size;
    if (!self || !mh01) return false;
    parsed = (xx_mh01_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_mh01_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (mh01->internal) {
        xx_mh01_private_cleanup((xx_mh01_private *)mh01->internal);
        xx_mem_free(mh01->internal);
    }
    mh01->internal = parsed;
    mh01->number_of_records = parsed->count;
    mh01->signature_offset = parsed->signature_offset;
    mh01->signature_size = parsed->signature_size;
    mh01->iv_size = parsed->iv_size;
    mh01->encrypted_data_size = parsed->encrypted_data_size;
    mh01->unknown1 = parsed->unknown1;
    mh01->unknown2 = parsed->unknown2;
    mh01->iv_offset = parsed->iv_offset;
    mh01->encrypted_data_offset = parsed->encrypted_data_offset;
    mh01->signature_data_offset = parsed->signature_data_offset;
    mh01->archive_end = parsed->archive_end;
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

int64_t xx_mh01_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_mh01_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_mh01 *)self)->number_of_records;
}

xx_archive_record_state *xx_mh01_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_mh01_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_mh01_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_mh01_copy_options(&state->options, options) ||
        !xx_mh01_parse(self, &stream->parsed, pd)) {
        xx_mh01_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_mh01_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_mh01_populate_record(&state->current_record, &stream->parsed,
                                &stream->parsed.regions[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_mh01_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mh01_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_mh01_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_mh01_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_mh01_populate_record(&state->current_record, &stream->parsed,
                                 &stream->parsed.regions[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_mh01_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_mh01_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the region's span is addressable. */
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
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
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_mh01_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_mh01_get_number_of_records(const xx_mh01 *mh01) {
    return mh01 ? mh01->number_of_records : 0U;
}
uint32_t xx_mh01_get_iv_size(const xx_mh01 *mh01) {
    return mh01 ? mh01->iv_size : 0U;
}
uint32_t xx_mh01_get_encrypted_data_size(const xx_mh01 *mh01) {
    return mh01 ? mh01->encrypted_data_size : 0U;
}
uint32_t xx_mh01_get_signature_size(const xx_mh01 *mh01) {
    return mh01 ? mh01->signature_size : 0U;
}
const char *xx_mh01_get_iv(const xx_mh01 *mh01) {
    const xx_mh01_private *parsed =
        mh01 ? (const xx_mh01_private *)mh01->internal : NULL;
    return parsed ? parsed->iv : NULL;
}
int64_t xx_mh01_get_archive_end(const xx_mh01 *mh01) {
    return mh01 ? mh01->archive_end : -1;
}
