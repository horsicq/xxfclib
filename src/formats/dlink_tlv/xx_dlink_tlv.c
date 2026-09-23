/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dlink_tlv/xx_dlink_tlv.h"

#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DLINK_TLV exists in the enum. */
#ifdef DLINK_TLV
#define XX_DLINK_TLV_FILE_TYPE XX_FILE_TYPE_DLINK_TLV
#else
#define XX_DLINK_TLV_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** The payload record's generated name.  Never taken from the file. */
#define XX_DLINK_TLV_PAYLOAD_NAME "payload"
/** Characters in an ASCII MD5, without a terminator. */
#define XX_DLINK_TLV_MD5_TEXT_SIZE 32U

typedef struct xx_dlink_tlv_private_s {
    char model_name[XX_DLINK_TLV_STRING_SIZE + 1U];
    char board_id[XX_DLINK_TLV_STRING_SIZE + 1U];
    char checksum[XX_DLINK_TLV_STRING_SIZE + 1U];
    int64_t input_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t archive_end;
    uint32_t raw_data_size;
    uint32_t data_type;
    bool checksum_present;
    bool checksum_verified;
} xx_dlink_tlv_private;

typedef struct xx_dlink_tlv_archive_stream_s {
    xx_dlink_tlv_private parsed;
    size_t index;
} xx_dlink_tlv_archive_stream;

static void xx_dlink_tlv_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: this header can sit at any offset in
 * a larger flash dump and long is 32-bit on Win64. */
static bool xx_dlink_tlv_read_at(xx_io_device *device, int64_t offset,
                                 void *data, size_t size) {
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

static bool xx_dlink_tlv_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dlink_tlv_range_within(int64_t total_size, int64_t offset,
                                      int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/*
 * Copy a fixed-width, NUL padded header string.  binwalk only requires the
 * model name and board ID to be non-empty; the extra requirement here is that
 * every byte up to the terminator be printable ASCII, which both fields are
 * on any genuine image and which keeps a random 0x74 block carrying the four
 * magic bytes from parsing as a header.
 */
static bool xx_dlink_tlv_copy_string(const uint8_t *header, size_t offset,
                                     char *out, bool allow_empty) {
    size_t index;
    for (index = 0U; index < XX_DLINK_TLV_STRING_SIZE; ++index) {
        uint8_t value = header[offset + index];
        if (value == 0U) break;
        if (value < 0x20U || value > 0x7EU) return false;
        out[index] = (char)value;
    }
    out[index] = '\0';
    return allow_empty || index != 0U;
}

static bool xx_dlink_tlv_is_hex_digit(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static char xx_dlink_tlv_lower(char value) {
    return (value >= 'A' && value <= 'Z') ? (char)(value + ('a' - 'A')) : value;
}

/*
 * Verify the header's ASCII MD5.
 *
 * The digest range starts eight bytes before the payload - at the TLV type
 * word - so the type and length are authenticated together with the data.
 * That offset is the single detail most likely to be got wrong, and getting
 * it wrong rejects every genuine image.
 */
static bool xx_dlink_tlv_verify_md5(xx_io_device *device, int64_t data_offset,
                                    int64_t data_size, const char *expected,
                                    xx_pd_struct *pd) {
    uint8_t digest[XX_MD5_DIGEST_SIZE];
    char text[2U * XX_MD5_DIGEST_SIZE + 1U];
    int64_t start = data_offset - (int64_t)XX_DLINK_TLV_CHECKSUM_PREFIX;
    int64_t span;
    size_t index;
    if (!device || !expected || start < 0) return false;
    span = data_size + (int64_t)XX_DLINK_TLV_CHECKSUM_PREFIX;
    if (!xx_hash_device(XX_HASH_MD5, device, start, span, digest,
                        sizeof(digest), pd) ||
        !xx_hash_to_hex(digest, sizeof(digest), text, sizeof(text))) {
        return false;
    }
    for (index = 0U; index < XX_DLINK_TLV_MD5_TEXT_SIZE; ++index) {
        if (xx_dlink_tlv_lower(expected[index]) != text[index]) return false;
    }
    return expected[XX_DLINK_TLV_MD5_TEXT_SIZE] == '\0';
}

static void xx_dlink_tlv_private_cleanup(xx_dlink_tlv_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_dlink_tlv_parse(Abstractformat *self,
                               xx_dlink_tlv_private *parsed, xx_pd_struct *pd) {
    uint8_t header[XX_DLINK_TLV_HEADER_SIZE];
    size_t index;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_dlink_tlv_range_within(parsed->input_size, self->base_address,
                                   (int64_t)XX_DLINK_TLV_HEADER_SIZE) ||
        !xx_dlink_tlv_read_at(self->device, self->base_address, header,
                              XX_DLINK_TLV_HEADER_SIZE) ||
        xx_data_get_u32(header, XX_DLINK_TLV_HEADER_SIZE, 0U, false) !=
            XX_DLINK_TLV_MAGIC) {
        goto fail;
    }
    if (!xx_dlink_tlv_copy_string(header, XX_DLINK_TLV_MODEL_NAME_OFFSET,
                                  parsed->model_name, false) ||
        !xx_dlink_tlv_copy_string(header, XX_DLINK_TLV_BOARD_ID_OFFSET,
                                  parsed->board_id, false) ||
        !xx_dlink_tlv_copy_string(header, XX_DLINK_TLV_MD5_OFFSET,
                                  parsed->checksum, true)) {
        goto fail;
    }

    /* The TLV that describes the payload.  Only type 1 is known; anything
     * else would be a layout this reader has no source for. */
    parsed->data_type = xx_data_get_u32(header, XX_DLINK_TLV_HEADER_SIZE,
                                        XX_DLINK_TLV_TLV_OFFSET, false);
    parsed->raw_data_size = xx_data_get_u32(header, XX_DLINK_TLV_HEADER_SIZE,
                                            XX_DLINK_TLV_TLV_OFFSET + 4U,
                                            false);
    if (parsed->data_type != XX_DLINK_TLV_DATA_TYPE ||
        parsed->raw_data_size == 0U) {
        goto fail;
    }

    /*
     * The declared length is attacker controlled, so it is bounded against
     * the device HERE, at parse time.  Nothing downstream - not the digest
     * pass, not an extraction - is allowed to size a read from an unchecked
     * header field.
     */
    if (!xx_dlink_tlv_add(self->base_address, XX_DLINK_TLV_HEADER_SIZE,
                          &parsed->data_offset)) {
        goto fail;
    }
    parsed->data_size = (int64_t)parsed->raw_data_size;
    if (!xx_dlink_tlv_range_within(parsed->input_size, parsed->data_offset,
                                   parsed->data_size) ||
        !xx_dlink_tlv_add(parsed->data_offset, (uint64_t)parsed->data_size,
                          &parsed->archive_end)) {
        goto fail;
    }

    /*
     * The digest field is optional: some images ship it as zeros, and binwalk
     * accepts those.  A field that is present must be 32 hex digits and must
     * match, otherwise the image is rejected - a wrong digest on a firmware
     * wrapper means the file is corrupt or forged, not merely unusual.
     */
    if (parsed->checksum[0] != '\0') {
        for (index = 0U; index < XX_DLINK_TLV_MD5_TEXT_SIZE; ++index) {
            if (!xx_dlink_tlv_is_hex_digit(parsed->checksum[index])) goto fail;
        }
        if (parsed->checksum[XX_DLINK_TLV_MD5_TEXT_SIZE] != '\0') goto fail;
        if (!xx_dlink_tlv_verify_md5(self->device, parsed->data_offset,
                                     parsed->data_size, parsed->checksum, pd)) {
            goto fail;
        }
        parsed->checksum_present = true;
        parsed->checksum_verified = true;
    }
    return true;
fail:
    xx_dlink_tlv_private_cleanup(parsed);
    return false;
}

/*
 * handle_base_info has already parsed the header and hashed the whole
 * payload; hashing it again for every record walk costs a full pass over a
 * multi-megabyte image.  Reuse that result when it still describes this
 * device at this base address, and parse afresh otherwise.
 */
static bool xx_dlink_tlv_reuse_or_parse(Abstractformat *self,
                                        xx_dlink_tlv_private *parsed,
                                        xx_pd_struct *pd) {
    const xx_dlink_tlv *tlv = (const xx_dlink_tlv *)self;
    const xx_dlink_tlv_private *cached;
    int64_t expected_offset;
    if (!self || !parsed) return false;
    cached = (const xx_dlink_tlv_private *)tlv->internal;
    if (cached && self->device &&
        xx_dlink_tlv_add(self->base_address, XX_DLINK_TLV_HEADER_SIZE,
                         &expected_offset) &&
        cached->data_offset == expected_offset &&
        cached->input_size == xx_io_total_size(self->device) &&
        xx_dlink_tlv_range_within(cached->input_size, cached->data_offset,
                                  cached->data_size)) {
        /* xx_rt_memcpy rather than a struct assignment, which a compiler is
         * free to lower into a CRT memcpy call. */
        xx_rt_memcpy(parsed, cached, sizeof(*parsed));
        return true;
    }
    return xx_dlink_tlv_parse(self, parsed, pd);
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_dlink_tlv_copy_options(xx_list_s *destination,
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

static const xx_var *xx_dlink_tlv_find_option(const xx_list_s *options,
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

static bool xx_dlink_tlv_populate_record(xx_archive_record *record,
                                         const xx_dlink_tlv_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        parsed->data_offset - (int64_t)XX_DLINK_TLV_HEADER_SIZE;
    record->header_size = (int64_t)XX_DLINK_TLV_HEADER_SIZE;
    record->data_offset = parsed->data_offset;
    record->compressed_size = parsed->data_size;
    /* The payload is stored verbatim, so compressed and uncompressed sizes
     * agree and the compression method is "none". */
    return xx_archive_record_set_original_name(record,
                                               XX_DLINK_TLV_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_dlink_tlv_archive_stream_free(void *pointer) {
    xx_dlink_tlv_archive_stream *stream =
        (xx_dlink_tlv_archive_stream *)pointer;
    if (!stream) return;
    xx_dlink_tlv_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_dlink_tlv_init(xx_dlink_tlv *tlv, xx_io_device *dev,
                       int64_t base_address) {
    if (!tlv) return;
    xx_mem_zero(tlv, sizeof(*tlv));
    xx_format_init(&tlv->format, dev, base_address);
    /* Little endian, unlike SEAMA and DLOB. */
    tlv->format.endian = XX_ENDIAN_LITTLE;
    tlv->format.file_type = XX_DLINK_TLV_FILE_TYPE;
    tlv->format.format_type = XX_TYPE_ARCHIVE;
    tlv->format.is_archive = true;
    xx_format_set_mime_type(&tlv->format, "application/x-dlink-tlv-firmware");
    xx_format_set_extension(&tlv->format, "bin");
    tlv->format.check_is_valid = xx_dlink_tlv_check_is_valid;
    tlv->format.handle_base_info = xx_dlink_tlv_handle_base_info;
    tlv->format.get_format_size = xx_dlink_tlv_get_format_size;
    tlv->format.get_number_of_archive_records =
        xx_dlink_tlv_get_number_of_archive_records;
    tlv->format.create_archive_records_reading =
        xx_dlink_tlv_create_archive_records_reading;
    tlv->format.get_current_archive_record =
        xx_dlink_tlv_get_current_archive_record;
    tlv->format.unpack_current_archive_record =
        xx_dlink_tlv_unpack_current_archive_record;
    tlv->format.archive_record_move_to_next =
        xx_dlink_tlv_archive_record_move_to_next;
    tlv->format.free_archive_records_reading =
        xx_dlink_tlv_free_archive_records_reading;
    tlv->format.destroy = xx_dlink_tlv_vtable_destroy;
    tlv->header_size = XX_DLINK_TLV_HEADER_SIZE;
    tlv->archive_end = -1;
}

xx_dlink_tlv *xx_dlink_tlv_create(xx_io_device *dev, int64_t base_address) {
    xx_dlink_tlv *tlv = (xx_dlink_tlv *)xx_mem_alloc(sizeof(*tlv));
    if (tlv) xx_dlink_tlv_init(tlv, dev, base_address);
    return tlv;
}

void xx_dlink_tlv_destroy(xx_dlink_tlv *tlv) {
    if (!tlv) return;
    if (tlv->internal) {
        xx_dlink_tlv_private_cleanup((xx_dlink_tlv_private *)tlv->internal);
        xx_mem_free(tlv->internal);
        tlv->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&tlv->format);
}

static void xx_dlink_tlv_vtable_destroy(Abstractformat *self) {
    xx_dlink_tlv_destroy((xx_dlink_tlv *)self);
}

void xx_dlink_tlv_free(xx_dlink_tlv *tlv) {
    if (!tlv) return;
    xx_dlink_tlv_destroy(tlv);
    xx_mem_free(tlv);
}

bool xx_dlink_tlv_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dlink_tlv_private parsed;
    bool result = xx_dlink_tlv_parse(self, &parsed, pd);
    xx_dlink_tlv_private_cleanup(&parsed);
    return result;
}

bool xx_dlink_tlv_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dlink_tlv_private *parsed;
    xx_dlink_tlv *tlv = (xx_dlink_tlv *)self;
    int64_t total_size;
    if (!self || !tlv) return false;
    parsed = (xx_dlink_tlv_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dlink_tlv_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (tlv->internal) {
        xx_dlink_tlv_private_cleanup((xx_dlink_tlv_private *)tlv->internal);
        xx_mem_free(tlv->internal);
    }
    tlv->internal = parsed;
    tlv->number_of_records = 1U;
    tlv->number_of_members = 1U;
    tlv->data_size = parsed->raw_data_size;
    tlv->data_type = parsed->data_type;
    tlv->header_size = XX_DLINK_TLV_HEADER_SIZE;
    tlv->checksum_present = parsed->checksum_present;
    tlv->checksum_verified = parsed->checksum_verified;
    tlv->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dlink_tlv_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dlink_tlv_get_number_of_archive_records(Abstractformat *self,
                                                    xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dlink_tlv *)self)->number_of_records;
}

xx_archive_record_state *xx_dlink_tlv_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dlink_tlv_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream =
        (xx_dlink_tlv_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dlink_tlv_copy_options(&state->options, options) ||
        !xx_dlink_tlv_reuse_or_parse(self, &stream->parsed, pd)) {
        xx_dlink_tlv_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dlink_tlv_archive_stream_free;
    state->total_records = 1;
    if (xx_dlink_tlv_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dlink_tlv_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dlink_tlv_archive_record_move_to_next(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_dlink_tlv_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* The header describes exactly one TLV payload, so the first move always
     * ends the walk. */
    stream = (xx_dlink_tlv_archive_stream *)state->internal_state;
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_dlink_tlv_unpack_current_archive_record(Abstractformat *self,
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
    option =
        xx_dlink_tlv_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the payload's span is addressable. */
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
    if (!result) xx_rt_remove(destination);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_dlink_tlv_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dlink_tlv_get_number_of_records(const xx_dlink_tlv *tlv) {
    return tlv ? tlv->number_of_records : 0U;
}
uint64_t xx_dlink_tlv_get_number_of_members(const xx_dlink_tlv *tlv) {
    return tlv ? tlv->number_of_members : 0U;
}
uint32_t xx_dlink_tlv_get_data_size(const xx_dlink_tlv *tlv) {
    return tlv ? tlv->data_size : 0U;
}
int64_t xx_dlink_tlv_get_archive_end(const xx_dlink_tlv *tlv) {
    return tlv ? tlv->archive_end : -1;
}
const char *xx_dlink_tlv_get_model_name(const xx_dlink_tlv *tlv) {
    return (tlv && tlv->internal)
               ? ((const xx_dlink_tlv_private *)tlv->internal)->model_name
               : NULL;
}
const char *xx_dlink_tlv_get_board_id(const xx_dlink_tlv *tlv) {
    return (tlv && tlv->internal)
               ? ((const xx_dlink_tlv_private *)tlv->internal)->board_id
               : NULL;
}
const char *xx_dlink_tlv_get_checksum(const xx_dlink_tlv *tlv) {
    return (tlv && tlv->internal)
               ? ((const xx_dlink_tlv_private *)tlv->internal)->checksum
               : NULL;
}
