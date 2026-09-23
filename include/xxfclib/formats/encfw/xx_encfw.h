/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_encfw.h @brief Known D-Link encrypted firmware images. */

/* "encfw" is binwalk's name for a family of D-Link firmware images that carry
 * no cleartext header at all.  The whole file, from its first byte to its
 * last, is AES-256-CBC ciphertext under a per-device vendor key; the first
 * four bytes are simply the start of the first cipher block.  binwalk treats
 * them as a per-device constant and uses them as a device tag
 * (src/signatures/encfw.rs: medium confidence at offset 0, low elsewhere, no
 * other check):
 *
 *   +0   u8[4]  first four ciphertext bytes, one of five known values
 *   +0   ...    ciphertext, to end of file, a whole number of 16-byte blocks
 *
 *   df 8c 39 0d   D-Link DIR-822 rev C
 *   35 66 6f 68   D-Link DAP-1665
 *   f5 2a a0 b4   D-Link DIR-842 rev C
 *   e3 13 00 5b   D-Link DIR-850 rev A
 *   0a 14 e4 24   D-Link DIR-850 rev B
 *
 * That the tag is ciphertext, not a prefix, is established by the decryptor
 * binwalk calls: the `delink` crate's encimg.rs lists these five devices with
 * an encrypted-data offset of 0, hands encrypted_image[0..] - tag included,
 * to end of file - to an unpadded AES-256-CBC decrypt, and accepts the result
 * when the first four PLAINTEXT bytes are a SEAMA, device-tree or LZMA magic.
 * Its aes.rs refuses any input that is not a multiple of 16 bytes, so an image
 * delink can decrypt is always block aligned.
 *
 * NO DECRYPTION IS ATTEMPTED, EVER.  None of delink's key material is
 * reproduced here and no key recovery is performed.  This reader identifies
 * the container, names the device, publishes the whole ciphertext as one
 * record flagged XX_META_ID_IS_ENCRYPTED, and returns false from the unpack
 * entry point: "extracting" an encfw image yields no output at all.  That is
 * the same shape src/formats/luks/xx_luks.c takes for a volume whose
 * passphrase nobody supplied.
 *
 * The record ("payload.enc") carries:
 *   XX_META_ID_ENCRYPTION_METHOD  XX_ZIP_ENCRYPTION_AES_256 (4), the library's
 *                                 only AES-256 identifier; the mode is CBC
 *   XX_META_ID_COMMENT            "device=<name> tag=<8 hex digits>
 *                                 cipher=AES-256-CBC", e.g.
 *                                 "device=D-Link DAP-1665 tag=35666f68
 *                                 cipher=AES-256-CBC" (one line)
 * The tag itself is also available as xx_encfw_get_magic().  It is NOT an
 * encryption-method code and is not published as one.
 *
 * WEAK MAGIC.  Four bytes with nothing behind them is a weak signature: a
 * four-byte tag will occur by chance roughly once in every four gigabytes of
 * random data.  Three properties of genuine images stand in for the missing
 * header, and a candidate must have all of them:
 *
 *   - block alignment: the size from the tag to end of file is a multiple of
 *     16, the AES block size (see above);
 *   - a minimum size, since a real device firmware is megabytes;
 *   - ciphertext statistics: the first XX_ENCFW_SAMPLE_SIZE bytes must look
 *     uniformly random (nearly every byte value present, none over-
 *     represented).  AES-CBC output is, whatever the plaintext; text,
 *     tables, code and zero padding are not.
 *
 * A dispatcher MUST still give this format LAST refusal - after every format
 * with a real header and after every magic-less probe - since a compressed
 * or encrypted file of some other kind that happens to begin with one of the
 * five tags passes all three checks.
 */

#ifndef XXFCLIB_FORMAT_ENCFW_H
#define XXFCLIB_FORMAT_ENCFW_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_ENCFW_MAGIC_SIZE 4U

/** AES block size; the image is a whole number of these blocks. */
#define XX_ENCFW_BLOCK_SIZE 16

/** Smallest image (tag included) this reader will accept, see the note above.
 *  It is also the size of the statistics sample, so the sample is always
 *  there to read. */
#define XX_ENCFW_MIN_SIZE 4096

/** Bytes from the start of the image tested for ciphertext statistics. */
#define XX_ENCFW_SAMPLE_SIZE 4096

/** In XX_ENCFW_SAMPLE_SIZE uniformly random bytes the expected number of
 *  byte values that never occur is 256 * e^-16, about 3e-5, and the expected
 *  count of each value is 16 with a standard deviation of 4.  Over 500,000
 *  simulated samples the fewest distinct values seen was 255 and the largest
 *  single count 45, so these bounds leave a wide margin for ciphertext while
 *  still refusing text, code, tables and padding. */
#define XX_ENCFW_MIN_DISTINCT 240
#define XX_ENCFW_MAX_BYTE_COUNT 64

typedef struct xx_encfw xx_encfw;
typedef struct xx_encfw xx_encfw_t;
typedef struct xx_encfw XEncfw;

struct xx_encfw {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t magic;        /**< The device tag, read big endian so it prints in
                                order.  Ciphertext, not a method code. */
    int64_t payload_offset; /**< Start of the ciphertext: the image itself. */
    int64_t payload_size;   /**< Ciphertext size: the whole image. */
    const char *device_name; /**< Static string, never freed. */
    void *internal;
};

XXFC_API void xx_encfw_init(xx_encfw *encfw, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_encfw *xx_encfw_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_encfw_destroy(xx_encfw *encfw);
XXFC_API void xx_encfw_free(xx_encfw *encfw);

XXFC_API bool xx_encfw_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_encfw_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_encfw_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_encfw_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_encfw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_encfw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_encfw_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_encfw_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_encfw_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_encfw_get_device_name(const xx_encfw *encfw);
XXFC_API uint32_t xx_encfw_get_magic(const xx_encfw *encfw);
XXFC_API int64_t xx_encfw_get_payload_size(const xx_encfw *encfw);

static inline Abstractformat *xx_encfw_to_format(xx_encfw *encfw) {
    return encfw ? &encfw->format : NULL;
}
static inline void XEncfw_init(xx_encfw *encfw, xx_io_device *dev,
                               int64_t base_address) {
    xx_encfw_init(encfw, dev, base_address);
}
static inline xx_encfw *XEncfw_create(xx_io_device *dev, int64_t base_address) {
    return xx_encfw_create(dev, base_address);
}
static inline void XEncfw_free(xx_encfw *encfw) { xx_encfw_free(encfw); }
static inline bool XEncfw_is_valid(xx_encfw *encfw, xx_pd_struct *pd) {
    return encfw ? xx_format_is_valid(&encfw->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ENCFW_H */
