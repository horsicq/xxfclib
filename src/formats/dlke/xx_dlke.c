/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * D-Link DLKE signed-and-encrypted firmware wrapper.  The layout follows
 * binwalk's src/signatures/dlke.rs together with parse_jboot_arm_header() in
 * src/structures/jboot.rs; the field-by-field notes live in xx_dlke.h.
 *
 * NO DECRYPTION IS ATTEMPTED, EVER.  binwalk routes this format into the
 * `delink` crate's decryptor; none of that key material is reproduced here
 * and no key search is performed.  The encrypted region is listed, flagged
 * XX_META_ID_IS_ENCRYPTED and refused on unpack, the same shape
 * src/formats/luks/xx_luks.c takes.  The signature blob is plaintext and is
 * carved verbatim on request.
 *
 * The JBOOT ARM header parser below is a deliberate duplicate of the one in
 * src/formats/jboot/xx_jboot.c.  Sharing it would make a DLKE build depend on
 * the JBOOT translation unit being linked in, and the two formats are
 * registered independently; eighty lines of duplication is the cheaper of the
 * two mistakes.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dlke/xx_dlke.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as DLKE is registered there. */
#ifdef DLKE
#define XX_DLKE_FILE_TYPE XX_FILE_TYPE_DLKE
#else
#define XX_DLKE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_DLKE_ARM_MAGIC UINT32_C(0x4842) /**< "BH", u16 LE at +64. */

#define XX_DLKE_NAME_SIGNATURE "signature.bin"
#define XX_DLKE_NAME_PAYLOAD "payload.enc"

/* The two ROM ID strings binwalk keys on.  They are exactly twelve bytes,
 * which is the whole rom_id field, so there is no NUL terminator in the
 * file and the comparison is a fixed-length one. */
static const char xx_dlke_rom_ids[2][XX_DLKE_ROM_ID_SIZE] = {
    {'D', 'L', 'K', '6', 'E', '8', '2', '0', '2', '0', '0', '1'},
    {'D', 'L', 'K', '6', 'E', '6', '1', '1', '0', '0', '0', '2'}};

typedef struct xx_dlke_header_s {
    uint32_t data_size;
    uint32_t data_start;
    uint32_t erase_start;
    uint32_t erase_size;
    uint32_t timestamp;
} xx_dlke_header;

typedef struct xx_dlke_entry_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    int64_t header_offset;
    bool encrypted;
} xx_dlke_entry;

typedef struct xx_dlke_private_s {
    xx_dlke_entry entries[XX_DLKE_RECORD_COUNT];
    size_t count;
    int64_t input_size;
    int64_t archive_end;
    uint32_t signature_size;
    uint32_t payload_size;
    uint32_t timestamp;
    char rom_id[XX_DLKE_ROM_ID_SIZE + 1U];
} xx_dlke_private;

typedef struct xx_dlke_stream_s {
    xx_dlke_private parsed;
    size_t index;
} xx_dlke_stream;

static void xx_dlke_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: the container is bounded by 32-bit
 * length fields but its base address inside a larger flash dump is not, and
 * long is 32-bit on Win64. */
static bool xx_dlke_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_dlke_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dlke_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_dlke_private_cleanup(xx_dlke_private *parsed) {
    size_t index;

    if (!parsed) return;
    for (index = 0U; index < XX_DLKE_RECORD_COUNT; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static void xx_dlke_stream_free(void *pointer) {
    xx_dlke_stream *stream = (xx_dlke_stream *)pointer;

    if (!stream) return;
    xx_dlke_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* A full JBOOT ARM header validation.  Every must-be-zero field is checked:
 * without them the header has no magic at all in its first sixty-four bytes
 * and any twelve-byte string would look like a ROM ID. */
static bool xx_dlke_parse_arm_header(const uint8_t *header,
                                     xx_dlke_header *out) {
    const size_t size = XX_DLKE_HEADER_SIZE;

    if (!header || !out) return false;
    if (xx_data_get_u32(header, size, 20U, false) != 0U ||
        xx_data_get_u16(header, size, 24U, false) != 0U ||
        xx_data_get_u32(header, size, 48U, false) != 0U ||
        xx_data_get_u32(header, size, 52U, false) != 0U ||
        xx_data_get_u32(header, size, 56U, false) != 0U ||
        xx_data_get_u32(header, size, 60U, false) != 0U ||
        xx_data_get_u16(header, size, 68U, false) != 0U) {
        return false;
    }
    if (xx_data_get_u8(header, size, 26U) != 1U ||
        xx_data_get_u8(header, size, 27U) != 0U) {
        return false;
    }
    if (xx_data_get_u16(header, size, 64U, false) != XX_DLKE_ARM_MAGIC) {
        return false;
    }
    if (xx_data_get_u16(header, size, 66U, false) > 4U) return false;

    out->timestamp = xx_data_get_u32(header, size, 28U, false);
    out->erase_start = xx_data_get_u32(header, size, 32U, false);
    out->erase_size = xx_data_get_u32(header, size, 36U, false);
    out->data_start = xx_data_get_u32(header, size, 40U, false);
    out->data_size = xx_data_get_u32(header, size, 44U, false);
    return true;
}

static bool xx_dlke_rom_id_known(const uint8_t *header) {
    size_t index;

    for (index = 0U; index < 2U; ++index) {
        if (xx_rt_memcmp(header, xx_dlke_rom_ids[index], XX_DLKE_ROM_ID_SIZE) ==
            0) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------------- parse -- */

static bool xx_dlke_parse(Abstractformat *self, xx_dlke_private *parsed,
                          xx_pd_struct *pd) {
    uint8_t header[XX_DLKE_HEADER_SIZE];
    xx_dlke_header signature_header;
    xx_dlke_header crypt_header;
    int64_t crypt_header_offset;
    int64_t signature_offset;
    int64_t payload_offset;
    size_t index;

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

    if (!xx_dlke_range_within(parsed->input_size, self->base_address,
                              (int64_t)XX_DLKE_HEADER_SIZE) ||
        !xx_dlke_read_at(self->device, self->base_address, header,
                         XX_DLKE_HEADER_SIZE)) {
        goto fail;
    }
    /* The ROM ID is the only thing that separates DLKE from an ordinary JBOOT
     * ARM section header, so it is checked before anything else. */
    if (!xx_dlke_rom_id_known(header)) goto fail;
    if (!xx_dlke_parse_arm_header(header, &signature_header)) goto fail;

    for (index = 0U; index < XX_DLKE_ROM_ID_SIZE; ++index) {
        parsed->rom_id[index] = (char)header[index];
    }
    parsed->rom_id[XX_DLKE_ROM_ID_SIZE] = '\0';
    parsed->timestamp = signature_header.timestamp;
    parsed->signature_size = signature_header.data_size;

    /* Bound the declared signature size against the device HERE, before the
     * second header's position is computed from it: an unbounded data_size
     * would otherwise turn into an out-of-range seek. */
    if (!xx_dlke_add(self->base_address, XX_DLKE_HEADER_SIZE,
                     &signature_offset) ||
        !xx_dlke_range_within(parsed->input_size, signature_offset,
                              (int64_t)signature_header.data_size)) {
        goto fail;
    }
    if (!xx_dlke_add(signature_offset, signature_header.data_size,
                     &crypt_header_offset) ||
        !xx_dlke_range_within(parsed->input_size, crypt_header_offset,
                              (int64_t)XX_DLKE_HEADER_SIZE) ||
        !xx_dlke_read_at(self->device, crypt_header_offset, header,
                         XX_DLKE_HEADER_SIZE)) {
        goto fail;
    }
    if (!xx_dlke_parse_arm_header(header, &crypt_header)) goto fail;
    parsed->payload_size = crypt_header.data_size;

    if (!xx_dlke_add(crypt_header_offset, XX_DLKE_HEADER_SIZE,
                     &payload_offset) ||
        !xx_dlke_range_within(parsed->input_size, payload_offset,
                              (int64_t)crypt_header.data_size) ||
        !xx_dlke_add(payload_offset, crypt_header.data_size,
                     &parsed->archive_end)) {
        goto fail;
    }

    /* A DLKE image with nothing encrypted in it is not a DLKE image. */
    if (crypt_header.data_size == 0U) goto fail;

    parsed->entries[0].name = xx_str_create(XX_DLKE_NAME_SIGNATURE);
    parsed->entries[0].header_offset = self->base_address;
    parsed->entries[0].data_offset = signature_offset;
    parsed->entries[0].data_size = (int64_t)signature_header.data_size;
    parsed->entries[0].encrypted = false;

    parsed->entries[1].name = xx_str_create(XX_DLKE_NAME_PAYLOAD);
    parsed->entries[1].header_offset = crypt_header_offset;
    parsed->entries[1].data_offset = payload_offset;
    parsed->entries[1].data_size = (int64_t)crypt_header.data_size;
    parsed->entries[1].encrypted = true;

    if (!parsed->entries[0].name || !parsed->entries[1].name) goto fail;
    parsed->count = XX_DLKE_RECORD_COUNT;
    return true;
fail:
    xx_dlke_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------- records -- */

static bool xx_dlke_copy_options(xx_list_s *destination,
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

static const xx_var *xx_dlke_find_option(const xx_list_s *options,
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

static bool xx_dlke_populate_record(xx_archive_record *record,
                                    const xx_dlke_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = (int64_t)XX_DLKE_HEADER_SIZE;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           entry->encrypted);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_dlke_init(xx_dlke *dlke, xx_io_device *dev, int64_t base_address) {
    if (!dlke) return;
    xx_mem_zero(dlke, sizeof(*dlke));
    xx_format_init(&dlke->format, dev, base_address);
    dlke->format.endian = XX_ENDIAN_LITTLE;
    dlke->format.file_type = XX_DLKE_FILE_TYPE;
    dlke->format.format_type = XX_TYPE_ARCHIVE;
    dlke->format.is_archive = true;
    xx_format_set_mime_type(&dlke->format, "application/x-dlke-firmware");
    xx_format_set_extension(&dlke->format, "bin");
    dlke->format.check_is_valid = xx_dlke_check_is_valid;
    dlke->format.handle_base_info = xx_dlke_handle_base_info;
    dlke->format.get_format_size = xx_dlke_get_format_size;
    dlke->format.get_number_of_archive_records =
        xx_dlke_get_number_of_archive_records;
    dlke->format.create_archive_records_reading =
        xx_dlke_create_archive_records_reading;
    dlke->format.get_current_archive_record = xx_dlke_get_current_archive_record;
    dlke->format.unpack_current_archive_record =
        xx_dlke_unpack_current_archive_record;
    dlke->format.archive_record_move_to_next =
        xx_dlke_archive_record_move_to_next;
    dlke->format.free_archive_records_reading =
        xx_dlke_free_archive_records_reading;
    dlke->format.destroy = xx_dlke_vtable_destroy;
    dlke->signature_offset = -1;
    dlke->payload_offset = -1;
    dlke->archive_end = -1;
}

xx_dlke *xx_dlke_create(xx_io_device *dev, int64_t base_address) {
    xx_dlke *dlke = (xx_dlke *)xx_mem_alloc(sizeof(*dlke));

    if (dlke) xx_dlke_init(dlke, dev, base_address);
    return dlke;
}

void xx_dlke_destroy(xx_dlke *dlke) {
    if (!dlke) return;
    if (dlke->internal) {
        xx_dlke_private_cleanup((xx_dlke_private *)dlke->internal);
        xx_mem_free(dlke->internal);
        dlke->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dlke->format);
}

static void xx_dlke_vtable_destroy(Abstractformat *self) {
    xx_dlke_destroy((xx_dlke *)self);
}

void xx_dlke_free(xx_dlke *dlke) {
    if (!dlke) return;
    xx_dlke_destroy(dlke);
    xx_mem_free(dlke);
}

/* -------------------------------------------------------------- format -- */

bool xx_dlke_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dlke_private parsed;
    bool result = xx_dlke_parse(self, &parsed, pd);

    xx_dlke_private_cleanup(&parsed);
    return result;
}

bool xx_dlke_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dlke *dlke = (xx_dlke *)self;
    xx_dlke_private *parsed;
    int64_t total_size;

    if (!self || !dlke) return false;
    parsed = (xx_dlke_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dlke_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dlke->internal) {
        xx_dlke_private_cleanup((xx_dlke_private *)dlke->internal);
        xx_mem_free(dlke->internal);
    }
    dlke->internal = parsed;
    dlke->number_of_records = parsed->count;
    dlke->signature_size = parsed->signature_size;
    dlke->payload_size = parsed->payload_size;
    dlke->timestamp = parsed->timestamp;
    dlke->signature_offset = parsed->entries[0].data_offset;
    dlke->payload_offset = parsed->entries[1].data_offset;
    dlke->archive_end = parsed->archive_end;
    xx_rt_memcpy(dlke->rom_id, parsed->rom_id, sizeof(dlke->rom_id));
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

int64_t xx_dlke_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dlke_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dlke *)self)->number_of_records;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_dlke_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dlke_stream *stream;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dlke_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dlke_copy_options(&state->options, options) ||
        !xx_dlke_parse(self, &stream->parsed, pd)) {
        xx_dlke_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dlke_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_dlke_populate_record(&state->current_record,
                                &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dlke_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dlke_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_dlke_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dlke_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_dlke_populate_record(&state->current_record,
                                 &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_dlke_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_dlke_stream *stream;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (const xx_dlke_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    /* The refusal this reader exists to make.  The payload is ciphertext under
     * a key this library does not have and does not look for; writing it out
     * under a plausible name would misrepresent it as the firmware's contents.
     * XX_META_ID_OPT_PASSWORD is deliberately NOT honoured - there is no
     * passphrase involved, the key is a vendor secret. */
    if (stream->parsed.entries[stream->index].encrypted) return false;

    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_dlke_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the signature's span is addressable. */
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

void xx_dlke_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

const char *xx_dlke_get_rom_id(const xx_dlke *dlke) {
    return dlke ? dlke->rom_id : "";
}

uint32_t xx_dlke_get_signature_size(const xx_dlke *dlke) {
    return dlke ? dlke->signature_size : 0U;
}

uint32_t xx_dlke_get_payload_size(const xx_dlke *dlke) {
    return dlke ? dlke->payload_size : 0U;
}

int64_t xx_dlke_get_archive_end(const xx_dlke *dlke) {
    return dlke ? dlke->archive_end : -1;
}
