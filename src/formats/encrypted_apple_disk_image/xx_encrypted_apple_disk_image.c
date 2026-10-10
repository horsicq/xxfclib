/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Encrypted Apple disk image (hdiutil -encryption): the "encrcdsa" v2
 * header at the start of the file and the older "cdsaencr" v1 trailer at its
 * end. Field layout per the header comment in
 * xx_encrypted_apple_disk_image.h; written from the published structure
 * descriptions (vfdecrypt / readencrcdsa.py documentation) - no code from
 * the GPL libmirage filter was used.
 *
 * NO DECRYPTION IS ATTEMPTED. There is no passphrase input, no PBKDF2, no
 * key unwrapping. The reader identifies the wrapper, publishes what the
 * cleartext header says, lists the ciphertext as one record carrying
 * XX_META_ID_IS_ENCRYPTED, and returns false from the unpack entry point -
 * the same shape as src/formats/luks/xx_luks.c.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef ENCRYPTED_APPLE_DISK_IMAGE
#define XX_ENCRYPTED_APPLE_DISK_IMAGE_FILE_TYPE XX_FILE_TYPE_ENCRYPTED_APPLE_DISK_IMAGE
#else
#define XX_ENCRYPTED_APPLE_DISK_IMAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_EADI_V2_HEADER_SIZE 0x4CU
#define XX_EADI_V2_KEY_POINTER_SIZE 20U
#define XX_EADI_V2_MAX_KEYS 32U
/* A wrapped-key blob is a few hundred bytes; 1 MB is far past anything
 * hdiutil writes and keeps a hostile pointer from being taken seriously. */
#define XX_EADI_V2_MAX_KEY_SIZE UINT64_C(0x100000)
#define XX_EADI_V2_PASSPHRASE_HEADER_SIZE 104U
#define XX_EADI_V2_KEY_TYPE_PASSPHRASE 1U
#define XX_EADI_CSSM_ALGID_PKCS5_PBKDF2 0x67U

#define XX_EADI_V1_TRAILER_SIZE 0x4FCU

#define XX_EADI_MEMBER_NAME "payload.enc"

typedef struct xx_eadi_private_s {
    int64_t input_size;
    int64_t base_address;
    uint64_t format_size;    /**< Bytes from base_address. */
    uint64_t payload_offset; /**< Absolute device offset. */
    uint64_t payload_size;
    uint64_t data_length;
    uint32_t version;
    uint32_t block_iv_len;
    uint32_t block_mode;
    uint32_t block_algorithm;
    uint32_t key_bits;
    uint32_t block_size;
    uint32_t number_of_keys;
    uint32_t passphrase_keys;
    uint32_t kdf_iterations;
    uint8_t uuid[16];
    bool truncated;
} xx_eadi_private;

static void xx_eadi_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_eadi_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;

    if (!device || (!data && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static void xx_eadi_private_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

static void xx_eadi_append_text(char *buffer, size_t capacity, size_t *used, const char *text)
{
    size_t index = 0U;

    if (!text) return;
    while (text[index] != '\0' && *used + 1U < capacity) {
        buffer[*used] = text[index];
        ++(*used);
        ++index;
    }
    buffer[*used] = '\0';
}

static void xx_eadi_append_u64(char *buffer, size_t capacity, size_t *used, uint64_t value)
{
    char digits[21];
    size_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U && *used + 1U < capacity) {
        buffer[*used] = digits[--count];
        ++(*used);
    }
    buffer[*used] = '\0';
}

/* ---------------------------------------------------------------- parse -- */

static bool xx_eadi_parse_v2(xx_io_device *device, xx_eadi_private *parsed, uint64_t available)
{
    uint8_t header[XX_EADI_V2_HEADER_SIZE];
    uint8_t table[XX_EADI_V2_MAX_KEYS * XX_EADI_V2_KEY_POINTER_SIZE];
    uint64_t table_end;
    uint64_t data_offset;
    uint64_t declared;
    uint64_t end;
    size_t table_size;
    uint32_t index;

    if (available < XX_EADI_V2_HEADER_SIZE || !xx_eadi_read_at(device, parsed->base_address, header, sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header, "encrcdsa", 8U) != 0) return false;
    parsed->version = xx_data_get_u32(header, sizeof(header), 0x08U, true);
    if (parsed->version != 2U) return false;
    parsed->block_iv_len = xx_data_get_u32(header, sizeof(header), 0x0CU, true);
    parsed->block_mode = xx_data_get_u32(header, sizeof(header), 0x10U, true);
    parsed->block_algorithm = xx_data_get_u32(header, sizeof(header), 0x14U, true);
    parsed->key_bits = xx_data_get_u32(header, sizeof(header), 0x18U, true);
    xx_mem_copy(parsed->uuid, header + 0x24U, sizeof(parsed->uuid));
    parsed->block_size = xx_data_get_u32(header, sizeof(header), 0x34U, true);
    parsed->data_length = xx_data_get_u64(header, sizeof(header), 0x38U, true);
    data_offset = xx_data_get_u64(header, sizeof(header), 0x40U, true);
    parsed->number_of_keys = xx_data_get_u32(header, sizeof(header), 0x48U, true);

    if (parsed->block_iv_len == 0U || parsed->block_iv_len > 64U) return false;
    if (parsed->key_bits != 128U && parsed->key_bits != 192U && parsed->key_bits != 256U) {
        return false;
    }
    /* A power of two between 512 bytes and 1 MB. */
    if (parsed->block_size < 512U || parsed->block_size > 0x100000U || (parsed->block_size & (parsed->block_size - 1U)) != 0U) {
        return false;
    }
    if (parsed->number_of_keys == 0U || parsed->number_of_keys > XX_EADI_V2_MAX_KEYS) {
        return false;
    }
    table_size = (size_t)parsed->number_of_keys * XX_EADI_V2_KEY_POINTER_SIZE;
    table_end = (uint64_t)XX_EADI_V2_HEADER_SIZE + table_size;
    if (table_end > available || !xx_eadi_read_at(device, parsed->base_address + XX_EADI_V2_HEADER_SIZE, table, table_size)) {
        return false;
    }
    end = table_end;
    for (index = 0U; index < parsed->number_of_keys; ++index) {
        size_t at = (size_t)index * XX_EADI_V2_KEY_POINTER_SIZE;
        uint32_t type = xx_data_get_u32(table, table_size, at, true);
        uint64_t offset = xx_data_get_u64(table, table_size, at + 4U, true);
        uint64_t size = xx_data_get_u64(table, table_size, at + 12U, true);

        if (size == 0U || size > XX_EADI_V2_MAX_KEY_SIZE || offset < XX_EADI_V2_HEADER_SIZE || offset > available || size > available - offset) {
            return false;
        }
        if (offset + size > end) end = offset + size;
        if (type == XX_EADI_V2_KEY_TYPE_PASSPHRASE) {
            ++parsed->passphrase_keys;
            if (parsed->kdf_iterations == 0U && size >= XX_EADI_V2_PASSPHRASE_HEADER_SIZE) {
                uint8_t key[XX_EADI_V2_PASSPHRASE_HEADER_SIZE];
                if (!xx_eadi_read_at(device, parsed->base_address + (int64_t)offset, key, sizeof(key))) {
                    return false;
                }
                if (xx_data_get_u32(key, sizeof(key), 0U, true) == XX_EADI_CSSM_ALGID_PKCS5_PBKDF2) {
                    parsed->kdf_iterations = xx_data_get_u32(key, sizeof(key), 8U, true);
                }
            }
        }
    }
    if (data_offset < table_end || data_offset > available) return false;
    /* The ciphertext is whole blocks; the last one is padded. */
    if (parsed->data_length > UINT64_C(0x4000000000000000)) return false;
    declared = (parsed->data_length + parsed->block_size - 1U) / parsed->block_size * parsed->block_size;
    parsed->payload_size = declared;
    if (declared > available - data_offset) {
        parsed->payload_size = available - data_offset;
        parsed->truncated = true;
    }
    parsed->payload_offset = (uint64_t)parsed->base_address + data_offset;
    if (data_offset + parsed->payload_size > end) {
        end = data_offset + parsed->payload_size;
    }
    parsed->format_size = end;
    return true;
}

static bool xx_eadi_parse_v1(xx_io_device *device, xx_eadi_private *parsed, uint64_t available)
{
    uint8_t trailer[XX_EADI_V1_TRAILER_SIZE];
    int64_t trailer_offset;
    uint32_t salt_len;
    uint32_t wrapped_aes;
    uint32_t wrapped_hmac;
    uint32_t integrity;

    if (available < XX_EADI_V1_TRAILER_SIZE) return false;
    trailer_offset = parsed->base_address + (int64_t)available - (int64_t)XX_EADI_V1_TRAILER_SIZE;
    /* Cheap first: the signature is the file's last eight bytes. */
    if (!xx_eadi_read_at(device, trailer_offset + XX_EADI_V1_TRAILER_SIZE - 8, trailer, 8U) || xx_rt_memcmp(trailer, "cdsaencr", 8U) != 0) {
        return false;
    }
    if (!xx_eadi_read_at(device, trailer_offset, trailer, sizeof(trailer))) {
        return false;
    }
    parsed->kdf_iterations = xx_data_get_u32(trailer, sizeof(trailer), 48U, true);
    salt_len = xx_data_get_u32(trailer, sizeof(trailer), 52U, true);
    wrapped_aes = xx_data_get_u32(trailer, sizeof(trailer), 136U, true);
    wrapped_hmac = xx_data_get_u32(trailer, sizeof(trailer), 436U, true);
    integrity = xx_data_get_u32(trailer, sizeof(trailer), 740U, true);
    if (parsed->kdf_iterations == 0U || salt_len == 0U || salt_len > 48U || wrapped_aes == 0U || wrapped_aes > 296U || wrapped_hmac == 0U || wrapped_hmac > 300U ||
        integrity > 48U) {
        return false;
    }
    parsed->version = 1U;
    parsed->number_of_keys = 1U;
    parsed->passphrase_keys = 1U;
    parsed->payload_offset = (uint64_t)parsed->base_address;
    parsed->payload_size = available - XX_EADI_V1_TRAILER_SIZE;
    parsed->format_size = available;
    return true;
}

static bool xx_eadi_parse(Abstractformat *self, xx_eadi_private *parsed, xx_pd_struct *pd)
{
    int64_t total_size;
    uint64_t available;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < 8 || self->base_address > total_size - 8) return false;
    available = (uint64_t)(total_size - self->base_address);
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    if (xx_eadi_parse_v2(self->device, parsed, available)) return true;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = total_size;
    parsed->base_address = self->base_address;
    if (xx_eadi_parse_v1(self->device, parsed, available)) return true;
    return false;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_encrypted_apple_disk_image_init(xx_encrypted_apple_disk_image *image, xx_io_device *dev, int64_t base_address)
{
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    image->format.endian = XX_ENDIAN_BIG;
    image->format.file_type = XX_ENCRYPTED_APPLE_DISK_IMAGE_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-apple-diskimage");
    xx_format_set_extension(&image->format, "dmg");
    image->format.check_is_valid = xx_encrypted_apple_disk_image_check_is_valid;
    image->format.handle_base_info = xx_encrypted_apple_disk_image_handle_base_info;
    image->format.get_format_size = xx_encrypted_apple_disk_image_get_format_size;
    image->format.get_number_of_archive_records = xx_encrypted_apple_disk_image_get_number_of_archive_records;
    image->format.create_archive_records_reading = xx_encrypted_apple_disk_image_create_archive_records_reading;
    image->format.get_current_archive_record = xx_encrypted_apple_disk_image_get_current_archive_record;
    image->format.unpack_current_archive_record = xx_encrypted_apple_disk_image_unpack_current_archive_record;
    image->format.archive_record_move_to_next = xx_encrypted_apple_disk_image_archive_record_move_to_next;
    image->format.free_archive_records_reading = xx_encrypted_apple_disk_image_free_archive_records_reading;
    image->format.destroy = xx_eadi_vtable_destroy;
}

xx_encrypted_apple_disk_image *xx_encrypted_apple_disk_image_create(xx_io_device *dev, int64_t base_address)
{
    xx_encrypted_apple_disk_image *image = (xx_encrypted_apple_disk_image *)xx_mem_alloc(sizeof(*image));

    if (image) xx_encrypted_apple_disk_image_init(image, dev, base_address);
    return image;
}

void xx_encrypted_apple_disk_image_destroy(xx_encrypted_apple_disk_image *image)
{
    if (!image) return;
    if (image->internal) {
        xx_eadi_private_free(image->internal);
        image->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_eadi_vtable_destroy(Abstractformat *self)
{
    xx_encrypted_apple_disk_image_destroy((xx_encrypted_apple_disk_image *)self);
}

void xx_encrypted_apple_disk_image_free(xx_encrypted_apple_disk_image *image)
{
    if (!image) return;
    xx_encrypted_apple_disk_image_destroy(image);
    xx_mem_free(image);
}

/* --------------------------------------------------------------- format -- */

bool xx_encrypted_apple_disk_image_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_eadi_private parsed;

    return xx_eadi_parse(self, &parsed, pd);
}

bool xx_encrypted_apple_disk_image_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_encrypted_apple_disk_image *image = (xx_encrypted_apple_disk_image *)self;
    xx_eadi_private *parsed;

    if (!self) return false;
    parsed = (xx_eadi_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_eadi_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (image->internal) xx_eadi_private_free(image->internal);
    image->internal = parsed;
    image->number_of_records = 1U;
    image->payload_offset = parsed->payload_offset;
    image->payload_size = parsed->payload_size;
    image->data_length = parsed->data_length;
    image->version = parsed->version;
    image->key_bits = parsed->key_bits;
    image->block_size = parsed->block_size;
    image->number_of_keys = parsed->number_of_keys;
    image->kdf_iterations = parsed->kdf_iterations;
    image->truncated = parsed->truncated;
    xx_mem_copy(image->uuid, parsed->uuid, sizeof(image->uuid));
    self->format_size = (int64_t)parsed->format_size;
    if ((int64_t)parsed->format_size < parsed->input_size - self->base_address) {
        self->overlay_offset = self->base_address + self->format_size;
        self->overlay_size = parsed->input_size - self->base_address - self->format_size;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_encrypted_apple_disk_image_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_encrypted_apple_disk_image_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_encrypted_apple_disk_image *)self)->number_of_records;
}

/* -------------------------------------------------------------- records -- */

static bool xx_eadi_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;

    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool xx_eadi_populate_record(xx_archive_record *record, const xx_eadi_private *parsed)
{
    char detail[192];
    size_t used = 0U;

    if (!record || !parsed) return false;
    detail[0] = '\0';
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->base_address;
    record->data_offset = (int64_t)parsed->payload_offset;
    record->compressed_size = (int64_t)parsed->payload_size;
    if (parsed->version == 2U) {
        record->header_size = (int64_t)XX_EADI_V2_HEADER_SIZE + (int64_t)parsed->number_of_keys * (int64_t)XX_EADI_V2_KEY_POINTER_SIZE;
        xx_eadi_append_text(detail, sizeof(detail), &used, "encrcdsa v2 AES-");
        xx_eadi_append_u64(detail, sizeof(detail), &used, parsed->key_bits);
        xx_eadi_append_text(detail, sizeof(detail), &used, " block=");
        xx_eadi_append_u64(detail, sizeof(detail), &used, parsed->block_size);
        xx_eadi_append_text(detail, sizeof(detail), &used, " data_len=");
        xx_eadi_append_u64(detail, sizeof(detail), &used, parsed->data_length);
        xx_eadi_append_text(detail, sizeof(detail), &used, " keys=");
        xx_eadi_append_u64(detail, sizeof(detail), &used, parsed->number_of_keys);
        xx_eadi_append_text(detail, sizeof(detail), &used, " passphrase_keys=");
        xx_eadi_append_u64(detail, sizeof(detail), &used, parsed->passphrase_keys);
    } else {
        record->header_size = (int64_t)XX_EADI_V1_TRAILER_SIZE;
        xx_eadi_append_text(detail, sizeof(detail), &used, "cdsaencr v1");
    }
    if (parsed->kdf_iterations != 0U) {
        xx_eadi_append_text(detail, sizeof(detail), &used, " kdf=PBKDF2-HMAC-SHA1 iterations=");
        xx_eadi_append_u64(detail, sizeof(detail), &used, parsed->kdf_iterations);
    }
    if (parsed->truncated) {
        xx_eadi_append_text(detail, sizeof(detail), &used, " TRUNCATED");
    }
    xx_eadi_append_text(detail, sizeof(detail), &used, " ENCRYPTED");
    return xx_archive_record_set_original_name(record, XX_EADI_MEMBER_NAME) &&
           /* v2 records the plaintext length; v1 does not, so the ciphertext
            * extent is all there is to report. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, parsed->version == 2U ? parsed->data_length : parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, true) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ENCRYPTION_METHOD, parsed->version) && xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, detail);
}

xx_archive_record_state *xx_encrypted_apple_disk_image_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_eadi_private *parsed;

    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_eadi_private *)xx_mem_calloc(1U, sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_eadi_copy_options(&state->options, options) || !xx_eadi_parse(self, parsed, pd)) {
        xx_eadi_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_eadi_private_free;
    state->total_records = 1;
    if (!xx_eadi_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_encrypted_apple_disk_image_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_encrypted_apple_disk_image_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_encrypted_apple_disk_image_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    (void)self;
    (void)state;
    (void)pd;
    /* Every payload byte is AES ciphertext under a key wrapped by a
     * passphrase nobody has supplied. Writing it out under the member's name
     * would misrepresent it as the disk image's contents, so nothing is
     * written and no output file is ever created. */
    return false;
}

void xx_encrypted_apple_disk_image_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

uint32_t xx_encrypted_apple_disk_image_get_version(const xx_encrypted_apple_disk_image *image)
{
    return image ? image->version : 0U;
}
