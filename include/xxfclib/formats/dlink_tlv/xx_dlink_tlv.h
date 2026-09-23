/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dlink_tlv.h @brief D-Link TLV firmware header reader. */

/* This is the header D-Link puts on the newer (post-SEAMA) images for its
 * consumer routers and cameras.  It is a fixed 0x74 byte block: a four byte
 * magic, two identification strings, an ASCII MD5 of the payload, and finally
 * a type/length pair that describes the one blob which follows.  That blob is
 * an ordinary payload - a uImage, an LZMA kernel, a squashfs rootfs, or an
 * OpenSSL "Salted__" encrypted container - which other readers in this
 * library already handle.
 *
 * binwalk only DETECTS this header; src/signatures/dlink_tlv.rs has no
 * extractor.  This reader validates the header, verifies the MD5 and
 * publishes the payload as an archive record so a caller can recurse into it.
 *
 * The strings are bytes; the numeric fields are LITTLE endian, which is worth
 * stating because most of the other D-Link wrappers (SEAMA, DLOB) are big.
 *
 *   header (0x74 bytes)
 *     +0x00  4 bytes   magic 64 80 19 40
 *     +0x04  32 bytes  model name string, NUL padded
 *     +0x24  32 bytes  board ID string, NUL padded
 *     +0x44  8 bytes   not described by any source; ignored (binwalk too)
 *     +0x4C  32 bytes  ASCII lowercase MD5 of the checksum range, or zeros
 *     +0x6C  u32 LE    TLV type, must be 1
 *     +0x70  u32 LE    TLV length, the payload size
 *     +0x74            payload begins
 *
 * The MD5 range is the quirk.  It does NOT cover the payload alone: it starts
 * eight bytes EARLY, at the TLV type field (+0x6C), and runs to the end of
 * the payload.  The type and length words are therefore authenticated along
 * with the data.  Getting that range wrong rejects every genuine image.
 *
 * The digest field is optional - some images ship it as all zeros.  An empty
 * field means "unchecked" and is accepted, matching binwalk; a present field
 * must match or the image is rejected.
 *
 * Deliberately stricter than binwalk, to keep detection quiet: the three
 * strings must be printable ASCII up to their terminator, and a TLV length
 * of zero (a header with no payload) is refused.
 *
 * Source: binwalk src/structures/dlink_tlv.rs and src/signatures/dlink_tlv.rs.
 */

#ifndef XXFCLIB_FORMAT_DLINK_TLV_H
#define XXFCLIB_FORMAT_DLINK_TLV_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Magic as a little endian u32; the bytes on disk are 64 80 19 40. */
#define XX_DLINK_TLV_MAGIC UINT32_C(0x40198064)
#define XX_DLINK_TLV_HEADER_SIZE 0x74U
#define XX_DLINK_TLV_STRING_SIZE 0x20U
#define XX_DLINK_TLV_MODEL_NAME_OFFSET 0x04U
#define XX_DLINK_TLV_BOARD_ID_OFFSET 0x24U
#define XX_DLINK_TLV_MD5_OFFSET 0x4CU
#define XX_DLINK_TLV_TLV_OFFSET 0x6CU
/** The digest range starts this many bytes before the payload. */
#define XX_DLINK_TLV_CHECKSUM_PREFIX 8U
/** The only TLV type this header is known to carry. */
#define XX_DLINK_TLV_DATA_TYPE 1U

typedef struct xx_dlink_tlv xx_dlink_tlv;
typedef struct xx_dlink_tlv xx_dlink_tlv_t;
typedef struct xx_dlink_tlv XDlinkTlv;

struct xx_dlink_tlv {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t data_size;    /**< TLV length, the payload size. */
    uint32_t data_type;    /**< TLV type; always 1 on an accepted header. */
    uint32_t header_size;  /**< Always XX_DLINK_TLV_HEADER_SIZE. */
    bool checksum_present; /**< False when the MD5 field was all zeros. */
    bool checksum_verified;/**< True when a present MD5 matched. */
    int64_t archive_end;   /**< base_address + header + payload, or -1. */
    void *internal;
};

XXFC_API void xx_dlink_tlv_init(xx_dlink_tlv *tlv, xx_io_device *dev,
                                int64_t base_address);
XXFC_API xx_dlink_tlv *xx_dlink_tlv_create(xx_io_device *dev,
                                           int64_t base_address);
XXFC_API void xx_dlink_tlv_destroy(xx_dlink_tlv *tlv);
XXFC_API void xx_dlink_tlv_free(xx_dlink_tlv *tlv);

XXFC_API bool xx_dlink_tlv_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_dlink_tlv_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_dlink_tlv_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_dlink_tlv_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dlink_tlv_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dlink_tlv_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dlink_tlv_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dlink_tlv_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dlink_tlv_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dlink_tlv_get_number_of_records(const xx_dlink_tlv *tlv);
XXFC_API uint64_t xx_dlink_tlv_get_number_of_members(const xx_dlink_tlv *tlv);
XXFC_API uint32_t xx_dlink_tlv_get_data_size(const xx_dlink_tlv *tlv);
XXFC_API int64_t xx_dlink_tlv_get_archive_end(const xx_dlink_tlv *tlv);
/** @brief Model name string from the header, or NULL; owned by the reader. */
XXFC_API const char *xx_dlink_tlv_get_model_name(const xx_dlink_tlv *tlv);
/** @brief Board ID string from the header, or NULL. */
XXFC_API const char *xx_dlink_tlv_get_board_id(const xx_dlink_tlv *tlv);
/** @brief ASCII MD5 from the header, or an empty string when absent. */
XXFC_API const char *xx_dlink_tlv_get_checksum(const xx_dlink_tlv *tlv);

static inline Abstractformat *xx_dlink_tlv_to_format(xx_dlink_tlv *tlv) {
    return tlv ? &tlv->format : NULL;
}
static inline void XDlinkTlv_init(xx_dlink_tlv *tlv, xx_io_device *dev,
                                  int64_t base_address) {
    xx_dlink_tlv_init(tlv, dev, base_address);
}
static inline xx_dlink_tlv *XDlinkTlv_create(xx_io_device *dev,
                                             int64_t base_address) {
    return xx_dlink_tlv_create(dev, base_address);
}
static inline void XDlinkTlv_free(xx_dlink_tlv *tlv) { xx_dlink_tlv_free(tlv); }
static inline bool XDlinkTlv_is_valid(xx_dlink_tlv *tlv, xx_pd_struct *pd) {
    return tlv ? xx_format_is_valid(&tlv->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DLINK_TLV_H */
