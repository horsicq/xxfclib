/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_encrpted_img.h @brief D-Link "encrpted_img" encrypted firmware. */

/* Several D-Link images ship with the literal ASCII string "encrpted_img" -
 * the vendor's own spelling, missing the 'y' - at offset zero, followed by
 * the encrypted image.  There is no length field, no checksum, no version and
 * no key identifier: the tag is the entire header.
 *
 *   +0   char[12]  "encrpted_img"
 *   +12  ...       ciphertext, to end of file
 *
 * That is genuinely all that is established.  binwalk's
 * src/signatures/encrpted_img.rs matches the twelve bytes and its parser says
 * so in as many words - it validates nothing because there is nothing to
 * validate.  No further structure is invented here: any "version" or "size"
 * field this reader claimed to find would be a guess, and a guess in a
 * container reader turns into a record pointing at the wrong bytes.
 *
 * NO DECRYPTION IS ATTEMPTED, EVER.  binwalk routes this format into the
 * `delink` crate's decryptor; none of that key material is reproduced here
 * and no key recovery is performed.  This reader identifies the container,
 * publishes the ciphertext region as one record flagged
 * XX_META_ID_IS_ENCRYPTED, and returns false from the unpack entry point.
 * src/formats/luks/xx_luks.c is the model.
 *
 * The twelve-byte ASCII tag is a strong signature - it will not collide with
 * anything - so unlike encfw this format is safe at any dispatch position.
 */

#ifndef XXFCLIB_FORMAT_ENCRPTED_IMG_H
#define XXFCLIB_FORMAT_ENCRPTED_IMG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The vendor's misspelling is the magic; do not "fix" it. */
#define XX_ENCRPTED_IMG_MAGIC "encrpted_img"
#define XX_ENCRPTED_IMG_MAGIC_SIZE 12U

typedef struct xx_encrpted_img xx_encrpted_img;
typedef struct xx_encrpted_img xx_encrpted_img_t;
typedef struct xx_encrpted_img XEncrptedImg;

struct xx_encrpted_img {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset;
    int64_t payload_size;
    void *internal;
};

XXFC_API void xx_encrpted_img_init(xx_encrpted_img *image, xx_io_device *dev,
                                   int64_t base_address);
XXFC_API xx_encrpted_img *xx_encrpted_img_create(xx_io_device *dev,
                                                 int64_t base_address);
XXFC_API void xx_encrpted_img_destroy(xx_encrpted_img *image);
XXFC_API void xx_encrpted_img_free(xx_encrpted_img *image);

XXFC_API bool xx_encrpted_img_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_encrpted_img_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_encrpted_img_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_encrpted_img_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_encrpted_img_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_encrpted_img_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_encrpted_img_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_encrpted_img_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_encrpted_img_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API int64_t xx_encrpted_img_get_payload_size(const xx_encrpted_img *image);

static inline Abstractformat *xx_encrpted_img_to_format(
    xx_encrpted_img *image) {
    return image ? &image->format : NULL;
}
static inline void XEncrptedImg_init(xx_encrpted_img *image, xx_io_device *dev,
                                     int64_t base_address) {
    xx_encrpted_img_init(image, dev, base_address);
}
static inline xx_encrpted_img *XEncrptedImg_create(xx_io_device *dev,
                                                   int64_t base_address) {
    return xx_encrpted_img_create(dev, base_address);
}
static inline void XEncrptedImg_free(xx_encrpted_img *image) {
    xx_encrpted_img_free(image);
}
static inline bool XEncrptedImg_is_valid(xx_encrpted_img *image,
                                         xx_pd_struct *pd) {
    return image ? xx_format_is_valid(&image->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ENCRPTED_IMG_H */
