/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * D-Link "encrpted_img" encrypted firmware images.  The framing follows
 * binwalk's src/signatures/encrpted_img.rs and the `delink` decryptor it
 * calls, which is the whole of what is established about this container: a
 * twelve-byte ASCII tag, four uninterpreted bytes, and ciphertext from +16 to
 * end of file.  The notes live in xx_encrpted_img.h.
 *
 * NO DECRYPTION IS ATTEMPTED, EVER.  There is no key table here, no key
 * search, no passphrase input.  The reader identifies the container,
 * publishes the ciphertext region as one record carrying
 * XX_META_ID_IS_ENCRYPTED, and returns false from the unpack entry point.
 * src/formats/luks/xx_luks.c is the model.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/encrpted_img/xx_encrpted_img.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as ENCRPTED_IMG is registered
 * there. */
#ifdef ENCRPTED_IMG
#define XX_ENCRPTED_IMG_FILE_TYPE XX_FILE_TYPE_ENCRPTED_IMG
#else
#define XX_ENCRPTED_IMG_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_ENCRPTED_IMG_MEMBER_NAME "payload.enc"

typedef struct xx_encrpted_img_private_s {
    int64_t input_size;
    int64_t payload_offset;
    int64_t payload_size;
    bool consumed;
} xx_encrpted_img_private;

static void xx_encrpted_img_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: the base address inside a larger flash
 * dump is not bounded by any 32-bit field, and long is 32-bit on Win64. */
static bool xx_encrpted_img_read_at(xx_io_device *device, int64_t offset,
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

static void xx_encrpted_img_private_free(void *pointer) {
    /* The struct owns nothing but itself. */
    if (pointer) xx_mem_free(pointer);
}

/* --------------------------------------------------------------- parse -- */

static bool xx_encrpted_img_parse(Abstractformat *self,
                                  xx_encrpted_img_private *parsed,
                                  xx_pd_struct *pd) {
    uint8_t magic[XX_ENCRPTED_IMG_MAGIC_SIZE];
    int64_t span;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->payload_offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (parsed->input_size < self->base_address) return false;
    span = parsed->input_size - self->base_address;
    /* Strictly more than the header: a header with no ciphertext behind it
     * is not an image. */
    if (span <= (int64_t)XX_ENCRPTED_IMG_HEADER_SIZE) return false;

    if (!xx_encrpted_img_read_at(self->device, self->base_address, magic,
                                 XX_ENCRPTED_IMG_MAGIC_SIZE)) {
        return false;
    }
    if (xx_rt_memcmp(magic, XX_ENCRPTED_IMG_MAGIC,
                     XX_ENCRPTED_IMG_MAGIC_SIZE) != 0) {
        return false;
    }
    /* span > HEADER_SIZE and base_address + span == input_size, so neither
     * expression can overflow. */
    parsed->payload_offset =
        self->base_address + (int64_t)XX_ENCRPTED_IMG_HEADER_SIZE;
    parsed->payload_size = span - (int64_t)XX_ENCRPTED_IMG_HEADER_SIZE;
    return true;
}

/* ------------------------------------------------------------- records -- */

static bool xx_encrpted_img_copy_options(xx_list_s *destination,
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

static bool xx_encrpted_img_populate_record(
    xx_archive_record *record, const xx_encrpted_img_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset =
        parsed->payload_offset - (int64_t)XX_ENCRPTED_IMG_HEADER_SIZE;
    record->header_size = (int64_t)XX_ENCRPTED_IMG_HEADER_SIZE;
    record->data_offset = parsed->payload_offset;
    record->compressed_size = parsed->payload_size;
    /* The plaintext size is unknown and unknowable without the key, so the
     * ciphertext's own size is reported for both. */
    return xx_archive_record_set_original_name(record,
                                               XX_ENCRPTED_IMG_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           true);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_encrpted_img_init(xx_encrpted_img *image, xx_io_device *dev,
                          int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, dev, base_address);
    /* There is no multi-byte field to have an endianness; the tag is ASCII.
     * Little is recorded because the boards are little endian. */
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_ENCRPTED_IMG_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format,
                            "application/x-dlink-encrpted-img");
    xx_format_set_extension(&image->format, "bin");
    image->format.check_is_valid = xx_encrpted_img_check_is_valid;
    image->format.handle_base_info = xx_encrpted_img_handle_base_info;
    image->format.get_format_size = xx_encrpted_img_get_format_size;
    image->format.get_number_of_archive_records =
        xx_encrpted_img_get_number_of_archive_records;
    image->format.create_archive_records_reading =
        xx_encrpted_img_create_archive_records_reading;
    image->format.get_current_archive_record =
        xx_encrpted_img_get_current_archive_record;
    image->format.unpack_current_archive_record =
        xx_encrpted_img_unpack_current_archive_record;
    image->format.archive_record_move_to_next =
        xx_encrpted_img_archive_record_move_to_next;
    image->format.free_archive_records_reading =
        xx_encrpted_img_free_archive_records_reading;
    image->format.destroy = xx_encrpted_img_vtable_destroy;
    image->payload_offset = -1;
}

xx_encrpted_img *xx_encrpted_img_create(xx_io_device *dev,
                                        int64_t base_address) {
    xx_encrpted_img *image =
        (xx_encrpted_img *)xx_mem_alloc(sizeof(*image));

    if (image) xx_encrpted_img_init(image, dev, base_address);
    return image;
}

void xx_encrpted_img_destroy(xx_encrpted_img *image) {
    if (!image) return;
    if (image->internal) {
        xx_encrpted_img_private_free(image->internal);
        image->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_encrpted_img_vtable_destroy(Abstractformat *self) {
    xx_encrpted_img_destroy((xx_encrpted_img *)self);
}

void xx_encrpted_img_free(xx_encrpted_img *image) {
    if (!image) return;
    xx_encrpted_img_destroy(image);
    xx_mem_free(image);
}

/* -------------------------------------------------------------- format -- */

bool xx_encrpted_img_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_encrpted_img_private parsed;

    return xx_encrpted_img_parse(self, &parsed, pd);
}

bool xx_encrpted_img_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_encrpted_img *image = (xx_encrpted_img *)self;
    xx_encrpted_img_private *parsed;

    if (!self || !image) return false;
    parsed = (xx_encrpted_img_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_encrpted_img_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (image->internal) xx_encrpted_img_private_free(image->internal);
    image->internal = parsed;
    image->number_of_records = 1U;
    image->payload_offset = parsed->payload_offset;
    image->payload_size = parsed->payload_size;
    /* The ciphertext runs to end of file, so there is never an overlay. */
    self->format_size =
        (int64_t)XX_ENCRPTED_IMG_HEADER_SIZE + parsed->payload_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_encrpted_img_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_encrpted_img_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_encrpted_img *)self)->number_of_records;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_encrpted_img_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_encrpted_img_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_encrpted_img_private *)xx_mem_alloc(sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_encrpted_img_parse(self, parsed, pd)) {
        xx_encrpted_img_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_encrpted_img_private_free;
    state->total_records = 1;
    if (!xx_encrpted_img_copy_options(&state->options, options) ||
        !xx_encrpted_img_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_encrpted_img_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_encrpted_img_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_encrpted_img_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* There is exactly one member, so the first step is always the last. */
    parsed = (xx_encrpted_img_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_encrpted_img_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    (void)self;
    (void)state;
    (void)pd;
    /* The refusal this reader exists to make.  Every byte of the payload is
     * ciphertext under a vendor key this library does not hold and does not
     * look for; there is nothing to unpack, and copying the ciphertext out
     * under the member's name would misrepresent it as the firmware's
     * contents.  XX_META_ID_OPT_PASSWORD is deliberately NOT honoured: no
     * passphrase is involved in this scheme at all. */
    return false;
}

void xx_encrpted_img_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

int64_t xx_encrpted_img_get_payload_size(const xx_encrpted_img *image) {
    return image ? image->payload_size : 0;
}
