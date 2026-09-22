/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pea.h @brief PEA archive reader (identification only). */

#ifndef XXFCLIB_FORMAT_PEA_H
#define XXFCLIB_FORMAT_PEA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PEA archive.
 *
 * PEA (Giorgio Tani's Pack, Encrypt, Authenticate) is a layered container: an
 * archive header, then a chain of object and stream headers carrying their own
 * compression, authentication and encryption controls. The payload codecs are
 * not implemented here, so this reader identifies the container and reports
 * the header fields rather than enumerating members; XArchive's XPEA is an
 * XExternalArchive for the same reason.
 */
typedef struct xx_pea {
    Abstractformat format;
    uint8_t version;          /**< Archive header byte 2, 0..6. */
    uint8_t object_control;   /**< Archive header byte 3. */
    uint8_t compression;      /**< First stream's compression, 0..3. */
    uint8_t stream_control;   /**< First stream's control byte. */
    bool big_endian;          /**< Set by bit 0x80 of archive header byte 8. */
    int64_t first_stream_offset; /**< Offset of the first "POD\0" trigger. */
} xx_pea;

typedef xx_pea xx_pea_t;
typedef xx_pea XPea;

XXFC_API void xx_pea_init(xx_pea *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pea *xx_pea_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pea_destroy(xx_pea *archive);
XXFC_API void xx_pea_free(xx_pea *archive);

XXFC_API bool xx_pea_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pea_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pea_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pea_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API uint8_t xx_pea_get_version(const xx_pea *archive);
XXFC_API uint8_t xx_pea_get_compression(const xx_pea *archive);
XXFC_API bool xx_pea_is_big_endian(const xx_pea *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PEA_H */
