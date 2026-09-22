/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_shrs.h @brief D-Link SHRS encrypted firmware container reader. */

/*
 * SHRS is the AES-encrypted firmware wrapper used on a number of D-Link
 * devices.  As with MH01 there is no vendor documentation; the field map here
 * is binwalk's (src/structures/shrs.rs), which is the only public description.
 *
 * Unlike MH01 this one is BIG endian.
 *
 *   +0     u32   magic, "SHRS"
 *   +4     u32   a length field binwalk does not name
 *   +8     u32   encrypted_data_size
 *   +12    u8[16] AES IV
 *   +28    ...   the rest of the 0x6DC header: signature and digest material
 *                whose layout is not established by any source consulted here
 *   +0x6DC       the encrypted image, encrypted_data_size bytes
 *
 * The header length 0x6DC is a constant in binwalk, not a field, and this
 * reader keeps it as one.  The field at +4 is left unnamed and republished
 * raw: on the samples binwalk was built against it tracks the plaintext length
 * but nothing establishes that, so calling it "decrypted size" here would be a
 * guess dressed up as a fact.
 *
 * binwalk classifies this one as low confidence, raised to medium only when it
 * sits at offset zero, and that caution is warranted: "SHRS" plus one length
 * is very little to go on.  This reader tightens it by requiring the declared
 * payload to be physically present, which binwalk does not check - binwalk
 * reports header_size + data_size as the signature size even when the file is
 * far shorter.
 *
 * The payload cannot be decrypted here - the AES key is in the bootloader - so
 * it is published as one flagged record along with the IV.
 */

#ifndef XXFCLIB_FORMAT_SHRS_H
#define XXFCLIB_FORMAT_SHRS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "SHRS" read as a big endian u32. */
#define XX_SHRS_MAGIC UINT32_C(0x53485253)
/** Fixed header length; a constant in binwalk, not a field in the file. */
#define XX_SHRS_HEADER_SIZE 0x6DCU
/** Offset and length of the AES IV inside the header. */
#define XX_SHRS_IV_OFFSET 12U
#define XX_SHRS_IV_SIZE 16U
/** Records published: iv, encrypted. */
#define XX_SHRS_MAX_RECORDS 2U

typedef struct xx_shrs xx_shrs;
typedef struct xx_shrs xx_shrs_t;
typedef struct xx_shrs XShrs;

struct xx_shrs {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t header_size;         /**< Always XX_SHRS_HEADER_SIZE. */
    uint32_t encrypted_data_size; /**< The field at +8. */
    uint32_t field_4;             /**< The unnamed field at +4, raw. */
    uint8_t iv[XX_SHRS_IV_SIZE];
    int64_t encrypted_data_offset; /**< Absolute, or -1. */
    int64_t archive_end;           /**< base_address + total size, or -1. */
    void *internal;
};

XXFC_API void xx_shrs_init(xx_shrs *shrs, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_shrs *xx_shrs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_shrs_destroy(xx_shrs *shrs);
XXFC_API void xx_shrs_free(xx_shrs *shrs);

XXFC_API bool xx_shrs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_shrs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_shrs_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_shrs_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_shrs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_shrs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_shrs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_shrs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_shrs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_shrs_get_number_of_records(const xx_shrs *shrs);
XXFC_API uint32_t xx_shrs_get_encrypted_data_size(const xx_shrs *shrs);
/** Pointer to the 16 raw IV bytes, or NULL. */
XXFC_API const uint8_t *xx_shrs_get_iv(const xx_shrs *shrs);
XXFC_API int64_t xx_shrs_get_archive_end(const xx_shrs *shrs);

static inline Abstractformat *xx_shrs_to_format(xx_shrs *shrs) {
    return shrs ? &shrs->format : NULL;
}
static inline void XShrs_init(xx_shrs *shrs, xx_io_device *dev,
                              int64_t base_address) {
    xx_shrs_init(shrs, dev, base_address);
}
static inline xx_shrs *XShrs_create(xx_io_device *dev, int64_t base_address) {
    return xx_shrs_create(dev, base_address);
}
static inline void XShrs_free(xx_shrs *shrs) { xx_shrs_free(shrs); }
static inline bool XShrs_is_valid(xx_shrs *shrs, xx_pd_struct *pd) {
    return shrs ? xx_format_is_valid(&shrs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SHRS_H */
