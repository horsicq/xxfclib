/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Known D-Link encrypted firmware images.  The tag table and the whole of the
 * framing follow binwalk's src/signatures/encfw.rs; the extractor it pairs
 * with, src/extractors/encfw.rs, is a call into the `delink` crate's
 * decryptor and is deliberately NOT ported.  The per-field notes live in
 * xx_encfw.h.
 *
 * NO DECRYPTION IS ATTEMPTED, EVER.  There is no key table here, no key
 * search, no passphrase input.  The reader identifies the container, names
 * the device, publishes the ciphertext - the whole image, tag included, since
 * delink decrypts from byte 0 - as one record carrying
 * XX_META_ID_IS_ENCRYPTED, and returns false from the unpack entry point, so
 * extraction writes nothing.  src/formats/luks/xx_luks.c is the model.
 *
 * The record's XX_META_ID_ENCRYPTION_METHOD is XX_ZIP_ENCRYPTION_AES_256, the
 * library's one named AES-256 value; the device tag is not a method and goes
 * into XX_META_ID_COMMENT instead, next to the device name.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/encfw/xx_encfw.h"

#include "xxfclib/formats/zip/xx_zip.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as ENCFW is registered there. */
#ifdef ENCFW
#define XX_ENCFW_FILE_TYPE XX_FILE_TYPE_ENCFW
#else
#define XX_ENCFW_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_ENCFW_MEMBER_NAME "payload.enc"

/* What the record publishes as its encryption method.  Every image is
 * AES-256-CBC (delink's encimg.rs); the library's only AES-256 identifier is
 * the ZIP one, so that value is reused rather than a new number invented.
 * The CBC mode is spelled out in the record comment. */
#define XX_ENCFW_ENCRYPTION_METHOD ((uint64_t)XX_ZIP_ENCRYPTION_AES_256)
#define XX_ENCFW_CIPHER_NAME "AES-256-CBC"

/* Room for "device=" + the longest device name + " tag=" + 8 hex digits +
 * " cipher=" + the cipher name + NUL, with slack. */
#define XX_ENCFW_COMMENT_CAPACITY 96U

/* The statistics sample must lie inside every image the size floor admits. */
typedef char xx_encfw_sample_fits_check
    [(XX_ENCFW_SAMPLE_SIZE <= XX_ENCFW_MIN_SIZE &&
      XX_ENCFW_MAGIC_SIZE <= XX_ENCFW_SAMPLE_SIZE)
         ? 1
         : -1];

typedef struct xx_encfw_entry_s {
    uint8_t magic[XX_ENCFW_MAGIC_SIZE];
    const char *device;
} xx_encfw_entry;

/* The five tags binwalk knows, with the make and model each one identifies. */
static const xx_encfw_entry xx_encfw_table[] = {
    {{0xdfU, 0x8cU, 0x39U, 0x0dU}, "D-Link DIR-822 rev C"},
    {{0x35U, 0x66U, 0x6fU, 0x68U}, "D-Link DAP-1665"},
    {{0xf5U, 0x2aU, 0xa0U, 0xb4U}, "D-Link DIR-842 rev C"},
    {{0xe3U, 0x13U, 0x00U, 0x5bU}, "D-Link DIR-850 rev A"},
    {{0x0aU, 0x14U, 0xe4U, 0x24U}, "D-Link DIR-850 rev B"}};

#define XX_ENCFW_TABLE_COUNT \
    (sizeof(xx_encfw_table) / sizeof(xx_encfw_table[0]))

typedef struct xx_encfw_private_s {
    int64_t input_size;
    int64_t payload_offset;
    int64_t payload_size;
    uint32_t magic;
    const char *device;
    bool consumed;
} xx_encfw_private;

static void xx_encfw_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: the base address inside a larger flash
 * dump is not bounded by any 32-bit field, and long is 32-bit on Win64. */
static bool xx_encfw_read_at(xx_io_device *device, int64_t offset, void *data,
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

static const char *xx_encfw_lookup(const uint8_t *magic) {
    size_t index;

    for (index = 0U; index < XX_ENCFW_TABLE_COUNT; ++index) {
        if (xx_rt_memcmp(magic, xx_encfw_table[index].magic,
                         XX_ENCFW_MAGIC_SIZE) == 0) {
            return xx_encfw_table[index].device;
        }
    }
    return NULL;
}

/* Streams the first XX_ENCFW_SAMPLE_SIZE bytes of the image through a byte
 * histogram and reports whether they look like block-cipher output.  The
 * caller has already established that the image is at least that long.  The
 * buffer is small and fixed; the read is bounded by the sample size. */
static bool xx_encfw_looks_like_ciphertext(xx_io_device *device,
                                           int64_t offset) {
    uint8_t buffer[512];
    uint16_t counts[256];
    size_t done = 0U;
    size_t index;
    unsigned distinct = 0U;

    xx_rt_memset(counts, 0, sizeof(counts));
    while (done < (size_t)XX_ENCFW_SAMPLE_SIZE) {
        size_t chunk = (size_t)XX_ENCFW_SAMPLE_SIZE - done;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (!xx_encfw_read_at(device, offset + (int64_t)done, buffer, chunk)) {
            return false;
        }
        for (index = 0U; index < chunk; ++index) {
            if (++counts[buffer[index]] > XX_ENCFW_MAX_BYTE_COUNT) {
                return false;
            }
        }
        done += chunk;
    }
    for (index = 0U; index < 256U; ++index) {
        if (counts[index] != 0U) ++distinct;
    }
    return distinct >= XX_ENCFW_MIN_DISTINCT;
}

/* Bounded append: never writes past capacity, always leaves a terminator. */
static void xx_encfw_append_text(char *buffer, size_t capacity, size_t *used,
                                 const char *text) {
    size_t index = 0U;

    if (!buffer || !used || capacity == 0U || *used >= capacity) return;
    if (text) {
        while (text[index] != '\0' && *used + 1U < capacity) {
            buffer[*used] = text[index];
            ++(*used);
            ++index;
        }
    }
    buffer[*used] = '\0';
}

/* The tag as eight lowercase hex digits, in file order. */
static void xx_encfw_append_tag(char *buffer, size_t capacity, size_t *used,
                                uint32_t magic) {
    static const char digits[] = "0123456789abcdef";
    char text[9];
    unsigned index;

    for (index = 0U; index < 8U; ++index) {
        text[index] = digits[(magic >> (28U - 4U * index)) & 0x0FU];
    }
    text[8] = '\0';
    xx_encfw_append_text(buffer, capacity, used, text);
}

/* "device=<name> tag=<8 hex digits> cipher=AES-256-CBC" */
static void xx_encfw_describe(char *buffer, size_t capacity,
                              const xx_encfw_private *parsed) {
    size_t used = 0U;

    if (!buffer || capacity == 0U) return;
    buffer[0] = '\0';
    xx_encfw_append_text(buffer, capacity, &used, "device=");
    xx_encfw_append_text(buffer, capacity, &used, parsed->device);
    xx_encfw_append_text(buffer, capacity, &used, " tag=");
    xx_encfw_append_tag(buffer, capacity, &used, parsed->magic);
    xx_encfw_append_text(buffer, capacity, &used, " cipher=");
    xx_encfw_append_text(buffer, capacity, &used, XX_ENCFW_CIPHER_NAME);
}

static void xx_encfw_private_free(void *pointer) {
    /* The device name is a static string and the struct owns nothing else. */
    if (pointer) xx_mem_free(pointer);
}

/* --------------------------------------------------------------- parse -- */

static bool xx_encfw_parse(Abstractformat *self, xx_encfw_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t magic[XX_ENCFW_MAGIC_SIZE];
    const char *device;
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

    /* The four-byte tag carries no structure to check it against, so what a
     * genuine image must also be stands in for it (see the WEAK MAGIC note in
     * xx_encfw.h).  The cheap checks run first: size and alignment need no
     * read, the tag needs four bytes, and only a file that carries a known
     * tag pays for the statistics sample. */
    if (span < (int64_t)XX_ENCFW_MIN_SIZE ||
        (span % (int64_t)XX_ENCFW_BLOCK_SIZE) != 0) {
        return false;
    }
    if (!xx_encfw_read_at(self->device, self->base_address, magic,
                          XX_ENCFW_MAGIC_SIZE)) {
        return false;
    }
    device = xx_encfw_lookup(magic);
    if (!device) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (!xx_encfw_looks_like_ciphertext(self->device, self->base_address)) {
        return false;
    }

    parsed->device = device;
    /* Read big endian purely so the value prints in file order. */
    parsed->magic = ((uint32_t)magic[0] << 24) | ((uint32_t)magic[1] << 16) |
                    ((uint32_t)magic[2] << 8) | (uint32_t)magic[3];
    /* The tag is the start of the first cipher block, so the ciphertext is
     * the whole image. */
    parsed->payload_offset = self->base_address;
    parsed->payload_size = span;
    return true;
}

/* ------------------------------------------------------------- records -- */

static bool xx_encfw_copy_options(xx_list_s *destination,
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

static bool xx_encfw_populate_record(xx_archive_record *record,
                                     const xx_encfw_private *parsed) {
    char detail[XX_ENCFW_COMMENT_CAPACITY];

    if (!record || !parsed || !parsed->device) return false;
    xx_encfw_describe(detail, sizeof(detail), parsed);
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    /* There is no cleartext header: the tag is ciphertext too. */
    record->header_offset = parsed->payload_offset;
    record->header_size = 0;
    record->data_offset = parsed->payload_offset;
    record->compressed_size = parsed->payload_size;
    /* The plaintext size is unknown and unknowable without the key, so the
     * ciphertext's own size is reported for both.  It is not a guess at what
     * the plaintext would be: it is the extent of the bytes that are there. */
    return xx_archive_record_set_original_name(record, XX_ENCFW_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           true) &&
           /* The cipher, not the tag: the tag is ciphertext, not a code. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_ENCRYPTION_METHOD,
                                          XX_ENCFW_ENCRYPTION_METHOD) &&
           /* Device name and tag, for display. */
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, detail);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_encfw_init(xx_encfw *encfw, xx_io_device *dev, int64_t base_address) {
    if (!encfw) return;
    xx_mem_zero(encfw, sizeof(*encfw));
    xx_format_init(&encfw->format, dev, base_address);
    /* There is no multi-byte field to have an endianness; the tag is a byte
     * string.  Little is recorded because the boards are little endian. */
    encfw->format.endian = XX_ENDIAN_LITTLE;
    encfw->format.file_type = XX_ENCFW_FILE_TYPE;
    encfw->format.format_type = XX_TYPE_ARCHIVE;
    encfw->format.is_archive = true;
    xx_format_set_mime_type(&encfw->format,
                            "application/x-dlink-encrypted-firmware");
    xx_format_set_extension(&encfw->format, "bin");
    encfw->format.check_is_valid = xx_encfw_check_is_valid;
    encfw->format.handle_base_info = xx_encfw_handle_base_info;
    encfw->format.get_format_size = xx_encfw_get_format_size;
    encfw->format.get_number_of_archive_records =
        xx_encfw_get_number_of_archive_records;
    encfw->format.create_archive_records_reading =
        xx_encfw_create_archive_records_reading;
    encfw->format.get_current_archive_record =
        xx_encfw_get_current_archive_record;
    encfw->format.unpack_current_archive_record =
        xx_encfw_unpack_current_archive_record;
    encfw->format.archive_record_move_to_next =
        xx_encfw_archive_record_move_to_next;
    encfw->format.free_archive_records_reading =
        xx_encfw_free_archive_records_reading;
    encfw->format.destroy = xx_encfw_vtable_destroy;
    encfw->payload_offset = -1;
    encfw->device_name = "";
}

xx_encfw *xx_encfw_create(xx_io_device *dev, int64_t base_address) {
    xx_encfw *encfw = (xx_encfw *)xx_mem_alloc(sizeof(*encfw));

    if (encfw) xx_encfw_init(encfw, dev, base_address);
    return encfw;
}

void xx_encfw_destroy(xx_encfw *encfw) {
    if (!encfw) return;
    if (encfw->internal) {
        xx_encfw_private_free(encfw->internal);
        encfw->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&encfw->format);
}

static void xx_encfw_vtable_destroy(Abstractformat *self) {
    xx_encfw_destroy((xx_encfw *)self);
}

void xx_encfw_free(xx_encfw *encfw) {
    if (!encfw) return;
    xx_encfw_destroy(encfw);
    xx_mem_free(encfw);
}

/* -------------------------------------------------------------- format -- */

bool xx_encfw_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_encfw_private parsed;

    return xx_encfw_parse(self, &parsed, pd);
}

bool xx_encfw_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_encfw *encfw = (xx_encfw *)self;
    xx_encfw_private *parsed;

    if (!self || !encfw) return false;
    parsed = (xx_encfw_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_encfw_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (encfw->internal) xx_encfw_private_free(encfw->internal);
    encfw->internal = parsed;
    encfw->number_of_records = 1U;
    encfw->magic = parsed->magic;
    encfw->device_name = parsed->device;
    encfw->payload_offset = parsed->payload_offset;
    encfw->payload_size = parsed->payload_size;
    /* The ciphertext runs to end of file, so there is never an overlay. */
    self->format_size = parsed->payload_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_encfw_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_encfw_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_encfw *)self)->number_of_records;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_encfw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_encfw_private *parsed;

    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    parsed = (xx_encfw_private *)xx_mem_alloc(sizeof(*parsed));
    if (!state || !parsed) {
        if (state) xx_mem_free(state);
        if (parsed) xx_mem_free(parsed);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_encfw_parse(self, parsed, pd)) {
        xx_encfw_private_free(parsed);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = parsed;
    state->free_internal = xx_encfw_private_free;
    state->total_records = 1;
    if (!xx_encfw_copy_options(&state->options, options) ||
        !xx_encfw_populate_record(&state->current_record, parsed)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_encfw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_encfw_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_encfw_private *parsed;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* There is exactly one member, so the first step is always the last. */
    parsed = (xx_encfw_private *)state->internal_state;
    if (parsed) parsed->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_encfw_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    (void)self;
    (void)state;
    (void)pd;
    /* The refusal this reader exists to make.  Every byte of the image is
     * ciphertext under a vendor key this library does not hold and does not
     * look for; there is nothing to unpack, and copying the ciphertext out
     * under the member's name would misrepresent it as the firmware's
     * contents.  Nothing is written.  XX_META_ID_OPT_PASSWORD is deliberately
     * NOT honoured: no passphrase is involved in this scheme at all. */
    return false;
}

void xx_encfw_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ------------------------------------------------------------ accessors -- */

const char *xx_encfw_get_device_name(const xx_encfw *encfw) {
    return (encfw && encfw->device_name) ? encfw->device_name : "";
}

uint32_t xx_encfw_get_magic(const xx_encfw *encfw) {
    return encfw ? encfw->magic : 0U;
}

int64_t xx_encfw_get_payload_size(const xx_encfw *encfw) {
    return encfw ? encfw->payload_size : 0;
}
