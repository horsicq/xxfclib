/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_encfw.h @brief Known D-Link encrypted firmware images. */

/* "encfw" is binwalk's name for a family of D-Link firmware images that carry
 * no header at all beyond a four-byte tag identifying which device - and so
 * which vendor key - the image belongs to.  Everything after the tag is
 * ciphertext.
 *
 *   +0   u8[4]  device tag, one of five known values
 *   +4   ...    ciphertext, to end of file
 *
 *   df 8c 39 0d   D-Link DIR-822 rev C
 *   35 66 6f 68   D-Link DAP-1665
 *   f5 2a a0 b4   D-Link DIR-842 rev C
 *   e3 13 00 5b   D-Link DIR-850 rev A
 *   0a 14 e4 24   D-Link DIR-850 rev B
 *
 * NO DECRYPTION IS ATTEMPTED, EVER.  binwalk hands these images to the
 * `delink` crate, which holds the extracted vendor keys; none of that key
 * material is reproduced here and no key recovery is performed.  This reader
 * identifies the container, names the device, publishes the ciphertext region
 * as one record flagged XX_META_ID_IS_ENCRYPTED, and returns false from the
 * unpack entry point.  That is the same shape src/formats/luks/xx_luks.c takes
 * for a volume whose passphrase nobody supplied.
 *
 * WEAK MAGIC.  Four bytes with no structure behind them is the weakest
 * signature in this group by a wide margin: a four-byte tag will occur by
 * chance roughly once in every four gigabytes of random data, and there is no
 * second field to check it against.  Two things stand in for that missing
 * validation and neither is strong:
 *
 *   - a minimum payload size, since a real device firmware is megabytes and a
 *     coincidental four-byte hit in a small file is not one;
 *   - the position, since these tags are only meaningful at the very start of
 *     an image.
 *
 * A dispatcher MUST therefore give this format LAST refusal - after every
 * format with a real header - or it will occasionally claim someone else's
 * file.  See the port report.
 *
 * Whether the four tag bytes are themselves part of the ciphertext or a
 * cleartext prefix could not be established: binwalk passes the whole region
 * including the tag to the decryptor, which is consistent with either.  They
 * are treated as a four-byte header here, which is the interpretation that
 * makes the published record's extent meaningful; it does not matter to a
 * reader that never decrypts.
 */

#ifndef XXFCLIB_FORMAT_ENCFW_H
#define XXFCLIB_FORMAT_ENCFW_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_ENCFW_MAGIC_SIZE 4U

/** Smallest ciphertext region this reader will accept, see the note above. */
#define XX_ENCFW_MIN_PAYLOAD 4096

typedef struct xx_encfw xx_encfw;
typedef struct xx_encfw xx_encfw_t;
typedef struct xx_encfw XEncfw;

struct xx_encfw {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t magic;        /**< The tag, read big endian so it prints in order. */
    int64_t payload_offset;
    int64_t payload_size;
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
