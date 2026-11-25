/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_thomson_sap.h @brief Native Thomson SAP disk-image reader.
 * Reconstructs one disk.img by removing per-sector headers/checksums and
 * reversing the SAP payload XOR 0xB3. Source: HxC libsap producer format.
 * Supports SAP format 1 (80 tracks * 16 sectors * 256 bytes) and format 2
 * (40 tracks * 16 sectors * 128 bytes), single-sided standard healthy sectors.
 * Every sector identity and its Pukall CRC16 (reflected CCITT, initial FFFF)
 * is checked before listing and again before extraction. Protected sectors,
 * nonstandard/error flags, wrong sector order, missing sectors and CRC damage
 * are rejected. The inner Thomson DOS filesystem is a separate format.
 * Devices are borrowed. Input cursor is restored when tell is available.
 * Extraction streams through at most 64 KiB and honors cancellation,
 * MAX_MEMBER_SIZE and MEMORY_LIMIT options. No external backend is used.
 */
#ifndef XXFCLIB_FORMAT_THOMSON_SAP_H
#define XXFCLIB_FORMAT_THOMSON_SAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_thomson_sap xx_thomson_sap;
typedef struct xx_thomson_sap xx_thomson_sap_t;
struct xx_thomson_sap {
    Abstractformat format;
};
XXFC_API void xx_thomson_sap_init(xx_thomson_sap *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_thomson_sap *xx_thomson_sap_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_thomson_sap_destroy(xx_thomson_sap *reader);
XXFC_API void xx_thomson_sap_free(xx_thomson_sap *reader);
XXFC_API bool xx_thomson_sap_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_thomson_sap_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
/** Stream the zero-based member to a borrowed device at its current cursor.
 * Output must differ from input. Failure may leave partial output.
 */
XXFC_API bool xx_thomson_sap_unpack_to_device(xx_thomson_sap *reader, uint64_t record_index, xx_io_device *output, xx_pd_struct *pd);
static inline Abstractformat *xx_thomson_sap_to_format(xx_thomson_sap *reader)
{
    return reader ? &reader->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
