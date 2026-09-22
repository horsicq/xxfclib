/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dkbs.h @brief DKBS firmware header reader. */

/* DKBS is the firmware wrapper used by a family of Chinese set-top-box and
 * router SoC reference designs.  The header is a fixed 0xA0 byte block of
 * NUL terminated identification strings with one binary length field wedged
 * in the middle of them; everything after the header is one contiguous
 * payload, normally a uImage, an LZMA kernel or a squashfs rootfs that other
 * readers in this library already decode.
 *
 * binwalk only DETECTS this header - src/signatures/dkbs.rs has no extractor
 * and src/structures/dkbs.rs stops once the strings and the length parse.
 * This reader goes one step further and publishes the payload as an archive
 * record, which is the entire reason for porting it: a caller can recurse
 * into the payload instead of being told "there is a DKBS header here".
 *
 * The strings are plain bytes; the ONE numeric field is ambiguous.
 *
 *   header (0xA0 bytes)
 *     +0x00  32 bytes  board ID string, with the literal "_dkbs_" at +7
 *     +0x28  32 bytes  firmware version string
 *     +0x68  u32       payload size, endianness detected (see below)
 *     +0x70  32 bytes  boot device string
 *     +0xA0            payload begins
 *
 * The magic is the six bytes "_dkbs_" SEVEN bytes into the header, not at
 * the start: the board ID string begins with a short vendor prefix.  A
 * detector therefore has to look at header+7, and the reported start of the
 * image is magic_offset - 7.
 *
 * The size field carries no endianness marker.  binwalk resolves this by
 * reading it big endian first and accepting that reading when its top byte
 * is zero - i.e. when the big endian value is under 16 MiB, which every real
 * payload is - and falling back to little endian otherwise.  That heuristic
 * is ported verbatim, because guessing differently from binwalk on the same
 * image would be worse than sharing its blind spot.  Both readings are then
 * bounded against the device, so a wrong guess yields a rejection rather
 * than a runaway length.
 *
 * Source: binwalk src/structures/dkbs.rs and src/signatures/dkbs.rs.  No
 * vendor GPL drop describing this header was available, so nothing beyond
 * what those two files establish is claimed here.
 */

#ifndef XXFCLIB_FORMAT_DKBS_H
#define XXFCLIB_FORMAT_DKBS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DKBS_HEADER_SIZE 0xA0U
/** Byte offset of the "_dkbs_" literal inside the header. */
#define XX_DKBS_MAGIC_OFFSET 7U
#define XX_DKBS_MAGIC_SIZE 6U
#define XX_DKBS_STRING_SIZE 32U
#define XX_DKBS_BOARD_ID_OFFSET 0x00U
#define XX_DKBS_VERSION_OFFSET 0x28U
#define XX_DKBS_DATA_SIZE_OFFSET 0x68U
#define XX_DKBS_BOOT_DEVICE_OFFSET 0x70U

typedef struct xx_dkbs xx_dkbs;
typedef struct xx_dkbs xx_dkbs_t;
typedef struct xx_dkbs XDkbs;

struct xx_dkbs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t data_size;   /**< Payload length from +0x68. */
    uint32_t header_size; /**< Always XX_DKBS_HEADER_SIZE. */
    bool size_is_big_endian; /**< Which reading of +0x68 was accepted. */
    int64_t archive_end;  /**< base_address + header + payload, or -1. */
    void *internal;
};

XXFC_API void xx_dkbs_init(xx_dkbs *dkbs, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_dkbs *xx_dkbs_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dkbs_destroy(xx_dkbs *dkbs);
XXFC_API void xx_dkbs_free(xx_dkbs *dkbs);

XXFC_API bool xx_dkbs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dkbs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dkbs_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_dkbs_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dkbs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dkbs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dkbs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dkbs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dkbs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dkbs_get_number_of_records(const xx_dkbs *dkbs);
XXFC_API uint64_t xx_dkbs_get_number_of_members(const xx_dkbs *dkbs);
XXFC_API uint32_t xx_dkbs_get_data_size(const xx_dkbs *dkbs);
XXFC_API int64_t xx_dkbs_get_archive_end(const xx_dkbs *dkbs);
/** @brief Board ID string from the header, or NULL; owned by the reader. */
XXFC_API const char *xx_dkbs_get_board_id(const xx_dkbs *dkbs);
/** @brief Firmware version string from the header, or NULL. */
XXFC_API const char *xx_dkbs_get_version(const xx_dkbs *dkbs);
/** @brief Boot device string from the header, or NULL. */
XXFC_API const char *xx_dkbs_get_boot_device(const xx_dkbs *dkbs);

static inline Abstractformat *xx_dkbs_to_format(xx_dkbs *dkbs) {
    return dkbs ? &dkbs->format : NULL;
}
static inline void XDkbs_init(xx_dkbs *dkbs, xx_io_device *dev,
                              int64_t base_address) {
    xx_dkbs_init(dkbs, dev, base_address);
}
static inline xx_dkbs *XDkbs_create(xx_io_device *dev, int64_t base_address) {
    return xx_dkbs_create(dev, base_address);
}
static inline void XDkbs_free(xx_dkbs *dkbs) { xx_dkbs_free(dkbs); }
static inline bool XDkbs_is_valid(xx_dkbs *dkbs, xx_pd_struct *pd) {
    return dkbs ? xx_format_is_valid(&dkbs->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DKBS_H */
