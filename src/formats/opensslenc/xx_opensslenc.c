/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/opensslenc/xx_opensslenc.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_OPENSSL_ENC exists in the
 * enum. */
#ifdef OPENSSL_ENC
#define XX_OPENSSLENC_FILE_TYPE XX_FILE_TYPE_OPENSSL_ENC
#else
#define XX_OPENSSLENC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* binwalk's common::is_printable_ascii(): 0x0A..0x7E inclusive, so LF, VT,
 * FF and CR count as "printable" but TAB (0x09) does not. */
#define XX_OPENSSLENC_ASCII_MIN 0x0AU
#define XX_OPENSSLENC_ASCII_MAX 0x7EU

typedef struct xx_opensslenc_private_s {
    int64_t input_size;
    int64_t data_offset;
    int64_t data_size;
    uint64_t salt_value;
    uint8_t salt[XX_OPENSSLENC_SALT_SIZE];
} xx_opensslenc_private;

static void xx_opensslenc_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64, and a salted
 * blob is as likely to sit inside a firmware image as at offset zero. */
static bool xx_opensslenc_read_at(xx_io_device *device, int64_t offset,
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

static void xx_opensslenc_private_reset(xx_opensslenc_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->data_offset = -1;
    parsed->data_size = -1;
}

/*
 * binwalk (signatures/openssl.rs, is_salt_invalid) throws a hit away when
 * all eight salt bytes are NUL or printable ASCII: RAND_bytes() produces that
 * with probability (118/256)^8, about 1 in 500, while the literal text
 * "Salted__" followed by more text or by padding produces it every time.
 * structures/openssl.rs separately rejects a salt that is zero as a whole,
 * which is the all-NUL case of the same test.  The rule is copied exactly,
 * including its (tiny) false-negative rate on genuine files.
 */
bool xx_opensslenc_is_salt_implausible(const uint8_t *salt) {
    size_t index;
    if (!salt) return true;
    for (index = 0U; index < XX_OPENSSLENC_SALT_SIZE; ++index) {
        uint8_t byte = salt[index];
        if (byte != 0U && (byte < XX_OPENSSLENC_ASCII_MIN ||
                           byte > XX_OPENSSLENC_ASCII_MAX)) {
            return false;
        }
    }
    return true;
}

static bool xx_opensslenc_parse(Abstractformat *self,
                                xx_opensslenc_private *parsed,
                                xx_pd_struct *pd) {
    uint8_t header[XX_OPENSSLENC_HEADER_SIZE];
    xx_opensslenc_private_reset(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    /* Sixteen bytes have to be there: binwalk's structure parse fails on a
     * shorter tail and so does OpenSSL's own reader.  The ciphertext itself
     * may be empty - a stream-mode cipher (-aes-256-ctr, -chacha20, ...) on an
     * empty input writes exactly these sixteen bytes. */
    if (parsed->input_size < 0 || self->base_address > parsed->input_size ||
        parsed->input_size - self->base_address <
            (int64_t)XX_OPENSSLENC_HEADER_SIZE ||
        !xx_opensslenc_read_at(self->device, self->base_address, header,
                               sizeof(header)) ||
        xx_rt_memcmp(header, XX_OPENSSLENC_MAGIC,
                     XX_OPENSSLENC_MAGIC_SIZE) != 0) {
        goto fail;
    }
    xx_rt_memcpy(parsed->salt, header + XX_OPENSSLENC_MAGIC_SIZE,
                 XX_OPENSSLENC_SALT_SIZE);
    parsed->salt_value = xx_data_get_u64(header, sizeof(header),
                                         XX_OPENSSLENC_MAGIC_SIZE, true);
    if (parsed->salt_value == 0U ||
        xx_opensslenc_is_salt_implausible(parsed->salt)) {
        goto fail;
    }
    /* No length field exists, so the ciphertext is the rest of the device.
     * Block-mode output is a multiple of the block size and stream-mode
     * output is not, and the cipher is not recorded, so the length itself
     * cannot be checked either. */
    parsed->data_offset =
        self->base_address + (int64_t)XX_OPENSSLENC_HEADER_SIZE;
    parsed->data_size = parsed->input_size - parsed->data_offset;
    return true;
fail:
    xx_opensslenc_private_reset(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_opensslenc_init(xx_opensslenc *enc, xx_io_device *dev,
                        int64_t base_address) {
    if (!enc) return;
    xx_mem_zero(enc, sizeof(*enc));
    xx_format_init(&enc->format, dev, base_address);
    enc->format.endian = XX_ENDIAN_BIG;
    enc->format.file_type = XX_OPENSSLENC_FILE_TYPE;
    enc->format.format_type = XX_TYPE_RAW;
    enc->format.is_archive = false;
    xx_format_set_mime_type(&enc->format, "application/x-openssl-enc");
    xx_format_set_extension(&enc->format, "enc");
    enc->format.check_is_valid = xx_opensslenc_check_is_valid;
    enc->format.handle_base_info = xx_opensslenc_handle_base_info;
    enc->format.get_format_size = xx_opensslenc_get_format_size;
    enc->format.destroy = xx_opensslenc_vtable_destroy;
    enc->data_offset = -1;
    enc->data_size = -1;
}

xx_opensslenc *xx_opensslenc_create(xx_io_device *dev, int64_t base_address) {
    xx_opensslenc *enc = (xx_opensslenc *)xx_mem_alloc(sizeof(*enc));
    if (enc) xx_opensslenc_init(enc, dev, base_address);
    return enc;
}

void xx_opensslenc_destroy(xx_opensslenc *enc) {
    if (!enc) return;
    /* Nothing here owns heap memory beyond the base structure's extras. */
    xx_format_cleanup_extra_parameters(&enc->format);
}

static void xx_opensslenc_vtable_destroy(Abstractformat *self) {
    xx_opensslenc_destroy((xx_opensslenc *)self);
}

void xx_opensslenc_free(xx_opensslenc *enc) {
    if (!enc) return;
    xx_opensslenc_destroy(enc);
    xx_mem_free(enc);
}

bool xx_opensslenc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_opensslenc_private parsed;
    return xx_opensslenc_parse(self, &parsed, pd);
}

bool xx_opensslenc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_opensslenc_private parsed;
    xx_opensslenc *enc = (xx_opensslenc *)self;
    if (!self) return false;
    if (!xx_opensslenc_parse(self, &parsed, pd)) {
        xx_mem_zero(enc->salt, sizeof(enc->salt));
        enc->salt_value = 0U;
        enc->data_offset = -1;
        enc->data_size = -1;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    xx_rt_memcpy(enc->salt, parsed.salt, sizeof(enc->salt));
    enc->salt_value = parsed.salt_value;
    enc->data_offset = parsed.data_offset;
    enc->data_size = parsed.data_size;
    /* The carve runs to the end of the device, as binwalk's (size 0 means
     * "to the next signature or EOF").  There is no structural end to stop
     * at, so bytes appended to a salted file are indistinguishable from
     * ciphertext and no overlay is ever reported. */
    self->format_size = parsed.input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_opensslenc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_opensslenc_get_salt_value(const xx_opensslenc *enc) {
    return enc ? enc->salt_value : 0U;
}
const uint8_t *xx_opensslenc_get_salt(const xx_opensslenc *enc) {
    return enc && enc->format.base_info_handled ? enc->salt : NULL;
}
int64_t xx_opensslenc_get_data_offset(const xx_opensslenc *enc) {
    return enc ? enc->data_offset : -1;
}
int64_t xx_opensslenc_get_data_size(const xx_opensslenc *enc) {
    return enc ? enc->data_size : -1;
}
