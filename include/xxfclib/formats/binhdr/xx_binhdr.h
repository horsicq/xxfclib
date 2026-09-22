/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_binhdr.h @brief BINHDR ("U2ND") firmware header reader. */

/* The BINHDR header prefixes firmware images for Broadcom BCM47xx boards, as
 * shipped by the vendor CFE loader and recognised by binwalk's "BIN firmware
 * header" signature. It is a fixed 30-byte little-endian prologue; the
 * firmware payload simply follows it, with no length field of its own.
 *
 *   +0   char[4]  board id, four ASCII characters ("U12H" and friends)
 *   +4   u32      reserved, must be zero
 *   +8   u32      build date, packed vendor-specific
 *   +12  u8       firmware version, major
 *   +13  u8       firmware version, minor
 *   +14  u32      magic, the ASCII bytes "U2ND"
 *   +18  u8       hardware id: 0 = 4702, 1 = 4712, 2 = 4712L, 3 = 4704
 *   +19  u24      reserved, must be zero
 *   +22  u64      reserved, must be zero
 *
 * The three reserved fields, the four-byte printable board id and the closed
 * hardware-id set are what make the 4-byte magic usable as a detector; the
 * magic alone is far too short. Because the header carries no payload length,
 * the single record this reader exposes runs from the end of the header to
 * the end of the device, and the header is therefore reported as covering the
 * whole input rather than leaving an overlay.
 */

#ifndef XXFCLIB_FORMAT_BINHDR_H
#define XXFCLIB_FORMAT_BINHDR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_binhdr xx_binhdr;
typedef struct xx_binhdr xx_binhdr_t;
typedef struct xx_binhdr XBinhdr;

struct xx_binhdr {
    Abstractformat format;
    uint64_t number_of_records;  /**< Always 1 when the payload is non-empty. */
    uint64_t number_of_members;
    char board_id[8];        /**< The four board-id characters, NUL padded. */
    uint32_t build_date;     /**< Vendor-packed, not interpreted here. */
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t hardware_id;     /**< 0..3, see the layout note above. */
    int64_t payload_offset;  /**< base_address + 30, or -1. */
    int64_t payload_size;    /**< Bytes from payload_offset to end of device. */
    void *internal;
};

XXFC_API void xx_binhdr_init(xx_binhdr *binhdr, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_binhdr *xx_binhdr_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_binhdr_destroy(xx_binhdr *binhdr);
XXFC_API void xx_binhdr_free(xx_binhdr *binhdr);

XXFC_API bool xx_binhdr_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_binhdr_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_binhdr_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_binhdr_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_binhdr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_binhdr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_binhdr_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_binhdr_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_binhdr_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_binhdr_get_number_of_records(const xx_binhdr *binhdr);
XXFC_API const char *xx_binhdr_get_board_id(const xx_binhdr *binhdr);
XXFC_API uint32_t xx_binhdr_get_build_date(const xx_binhdr *binhdr);
XXFC_API uint8_t xx_binhdr_get_version_major(const xx_binhdr *binhdr);
XXFC_API uint8_t xx_binhdr_get_version_minor(const xx_binhdr *binhdr);
XXFC_API uint8_t xx_binhdr_get_hardware_id(const xx_binhdr *binhdr);
/** @brief "4702", "4712", "4712L", "4704", or NULL for an unknown id. */
XXFC_API const char *xx_binhdr_get_hardware_name(const xx_binhdr *binhdr);
XXFC_API int64_t xx_binhdr_get_payload_offset(const xx_binhdr *binhdr);
XXFC_API int64_t xx_binhdr_get_payload_size(const xx_binhdr *binhdr);

static inline Abstractformat *xx_binhdr_to_format(xx_binhdr *binhdr) {
    return binhdr ? &binhdr->format : NULL;
}
static inline void XBinhdr_init(xx_binhdr *binhdr, xx_io_device *dev,
                                int64_t base_address) {
    xx_binhdr_init(binhdr, dev, base_address);
}
static inline xx_binhdr *XBinhdr_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_binhdr_create(dev, base_address);
}
static inline void XBinhdr_free(xx_binhdr *binhdr) { xx_binhdr_free(binhdr); }
static inline bool XBinhdr_is_valid(xx_binhdr *binhdr, xx_pd_struct *pd) {
    return binhdr ? xx_format_is_valid(&binhdr->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BINHDR_H */
