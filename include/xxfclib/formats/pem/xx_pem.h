/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pem.h @brief PEM certificate / public key / private key reader. */

/* PEM (RFC 7468 "textual encoding") wraps a base64 DER blob between two
 * armour lines:
 *
 *     -----BEGIN CERTIFICATE-----
 *     MIIB...                        base64, usually 64 columns
 *     -----END CERTIFICATE-----
 *
 * The reader follows binwalk's src/signatures/pem.rs and
 * src/extractors/pem.rs to the byte, because the carve length binwalk
 * reports is the contract:
 *
 *  - A block starts with one of thirteen BEGIN lines (see xx_pem.c): four
 *    public key labels, eight private key labels and CERTIFICATE.  "BEGIN
 *    PGP ..." armour, CRLs, requests and parameter blocks are not PEM here.
 *  - It ends at the first of SEVEN END lines found anywhere after the block
 *    start, whatever the BEGIN label was, plus every CR/LF byte that follows.
 *    The END list is shorter than the BEGIN list, so e.g. a lone "RSA PUBLIC
 *    KEY" or "ENCRYPTED PRIVATE KEY" block has no end at all and is rejected,
 *    exactly as binwalk rejects it.
 *  - The carved bytes must be valid UTF-8, and the lines between the first
 *    two lines that start with "--" must concatenate to canonical, padded
 *    standard base64 (RFC 4648 section 4, zero trailing bits).  Anything
 *    else on those lines - RFC 1421 "Proc-Type:" headers included - fails.
 *
 * A file may hold several blocks back to back (certificate chains, key plus
 * certificate).  Each block is one archive record, named after its kind and
 * position (certificate_0.pem, private_key_1.pem, ...) and extracted as the
 * exact bytes binwalk carves.  The format ends after the last block that
 * follows its predecessor with nothing but spaces, tabs, CR or LF between
 * them; everything after it is overlay.
 */

#ifndef XXFCLIB_FORMAT_PEM_H
#define XXFCLIB_FORMAT_PEM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Shortest BEGIN line; also the length binwalk classifies on. */
#define XX_PEM_MIN_MAGIC_SIZE 26U
/** Longest BEGIN line ("-----BEGIN ENCRYPTED PRIVATE KEY-----"). */
#define XX_PEM_MAX_MAGIC_SIZE 37U
/** A block (BEGIN line to END line) longer than this is refused.  binwalk
 *  has no cap; real blocks are a few KiB, so this only bounds the scan. */
#define XX_PEM_MAX_BLOCK_SIZE (16 * 1024 * 1024)
/** Longest run of blanks tolerated between two blocks of one file. */
#define XX_PEM_MAX_GAP_SIZE (1024 * 1024)
/** Blocks past this count are left in the overlay. */
#define XX_PEM_MAX_BLOCKS 65536U

typedef enum xx_pem_kind_e {
    XX_PEM_KIND_NONE = 0,
    XX_PEM_KIND_CERTIFICATE = 1,
    XX_PEM_KIND_PUBLIC_KEY = 2,
    XX_PEM_KIND_PRIVATE_KEY = 3
} xx_pem_kind;

typedef struct xx_pem xx_pem;
typedef struct xx_pem xx_pem_t;
typedef struct xx_pem XPem;

struct xx_pem {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t number_of_certificates;
    uint64_t number_of_public_keys;
    uint64_t number_of_private_keys;
    uint32_t first_kind;       /**< xx_pem_kind of the first block. */
    int64_t first_block_size;  /**< binwalk's carve length of block 0. */
    int64_t archive_end;       /**< End of the last block, or -1. */
    void *internal;
};

XXFC_API void xx_pem_init(xx_pem *pem, xx_io_device *dev, int64_t base_address);
XXFC_API xx_pem *xx_pem_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_pem_destroy(xx_pem *pem);
XXFC_API void xx_pem_free(xx_pem *pem);

XXFC_API bool xx_pem_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pem_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pem_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_pem_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pem_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pem_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pem_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pem_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pem_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_pem_get_number_of_records(const xx_pem *pem);
XXFC_API uint64_t xx_pem_get_number_of_members(const xx_pem *pem);
XXFC_API uint32_t xx_pem_get_first_kind(const xx_pem *pem);
XXFC_API int64_t xx_pem_get_first_block_size(const xx_pem *pem);
XXFC_API int64_t xx_pem_get_archive_end(const xx_pem *pem);
/** "certificate", "public_key", "private_key" or NULL. */
XXFC_API const char *xx_pem_kind_to_string(uint32_t kind);

static inline Abstractformat *xx_pem_to_format(xx_pem *pem) {
    return pem ? &pem->format : NULL;
}
static inline void XPem_init(xx_pem *pem, xx_io_device *dev,
                             int64_t base_address) {
    xx_pem_init(pem, dev, base_address);
}
static inline xx_pem *XPem_create(xx_io_device *dev, int64_t base_address) {
    return xx_pem_create(dev, base_address);
}
static inline void XPem_free(xx_pem *pem) { xx_pem_free(pem); }
static inline bool XPem_is_valid(xx_pem *pem, xx_pd_struct *pd) {
    return pem ? xx_format_is_valid(&pem->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PEM_H */
