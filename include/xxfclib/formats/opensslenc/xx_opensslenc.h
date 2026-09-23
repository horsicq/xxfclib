/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_opensslenc.h @brief OpenSSL "enc" salted container reader. */

/* What `openssl enc -e -salt ...` writes when the output is binary (not -a):
 *
 *     +0x00  "Salted__", eight ASCII bytes, no terminator
 *     +0x08  the eight-byte salt, random bytes from RAND_bytes()
 *     +0x10  the ciphertext, to the end of the stream
 *
 * The salt is fed, with the password, to EVP_BytesToKey() (the default, with
 * -md choosing the digest) or PKCS5_PBKDF2_HMAC() (-pbkdf2 / -iter), and the
 * result is the key and IV.  Neither the cipher, the digest, the KDF nor the
 * iteration count is recorded anywhere in the file, and there is no length
 * field, no MAC and no trailer: the ciphertext simply runs to the end.  So
 * the container can be validated only by its prefix and salt, and its size is
 * "everything that is left".
 *
 * Source: binwalk's src/signatures/openssl.rs ("OpenSSL encryption") and
 * src/structures/openssl.rs, which read the same sixteen bytes and reject a
 * salt that is zero or made entirely of NUL and printable-ASCII bytes.
 * binwalk sets no size, so its carve runs to the next signature or EOF; this
 * reader reports the rest of the device.  binwalk's extractor for it
 * (encfw/delink) only tries D-Link's fixed firmware keys, so this is not an
 * archive: without the password there is nothing to extract.
 */

#ifndef XXFCLIB_FORMAT_OPENSSLENC_H
#define XXFCLIB_FORMAT_OPENSSLENC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_OPENSSLENC_MAGIC "Salted__"
#define XX_OPENSSLENC_MAGIC_SIZE 8U
#define XX_OPENSSLENC_SALT_SIZE 8U
/** Magic plus salt; the ciphertext starts right behind it. */
#define XX_OPENSSLENC_HEADER_SIZE 16U

typedef struct xx_opensslenc xx_opensslenc;
typedef struct xx_opensslenc xx_opensslenc_t;
typedef struct xx_opensslenc XOpensslenc;

struct xx_opensslenc {
    Abstractformat format;
    uint8_t salt[XX_OPENSSLENC_SALT_SIZE]; /**< The salt bytes, as stored. */
    uint64_t salt_value;     /**< The salt read as a big-endian u64. */
    int64_t data_offset;     /**< base_address + 16, or -1. */
    int64_t data_size;       /**< Ciphertext bytes (may be 0), or -1. */
};

XXFC_API void xx_opensslenc_init(xx_opensslenc *enc, xx_io_device *dev,
                                 int64_t base_address);
XXFC_API xx_opensslenc *xx_opensslenc_create(xx_io_device *dev,
                                             int64_t base_address);
XXFC_API void xx_opensslenc_destroy(xx_opensslenc *enc);
XXFC_API void xx_opensslenc_free(xx_opensslenc *enc);

XXFC_API bool xx_opensslenc_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_opensslenc_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_opensslenc_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);

/** True when binwalk would reject this salt (see xx_opensslenc.c). */
XXFC_API bool xx_opensslenc_is_salt_implausible(const uint8_t *salt);

XXFC_API uint64_t xx_opensslenc_get_salt_value(const xx_opensslenc *enc);
XXFC_API const uint8_t *xx_opensslenc_get_salt(const xx_opensslenc *enc);
XXFC_API int64_t xx_opensslenc_get_data_offset(const xx_opensslenc *enc);
XXFC_API int64_t xx_opensslenc_get_data_size(const xx_opensslenc *enc);

static inline Abstractformat *xx_opensslenc_to_format(xx_opensslenc *enc) {
    return enc ? &enc->format : NULL;
}
static inline void XOpensslenc_init(xx_opensslenc *enc, xx_io_device *dev,
                                    int64_t base_address) {
    xx_opensslenc_init(enc, dev, base_address);
}
static inline xx_opensslenc *XOpensslenc_create(xx_io_device *dev,
                                                int64_t base_address) {
    return xx_opensslenc_create(dev, base_address);
}
static inline void XOpensslenc_free(xx_opensslenc *enc) {
    xx_opensslenc_free(enc);
}
static inline bool XOpensslenc_is_valid(xx_opensslenc *enc, xx_pd_struct *pd) {
    return enc ? xx_format_is_valid(&enc->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_OPENSSLENC_H */
