/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_d_link_alpha_encimg_v2.h
 * @brief D-Link / Alpha Networks "encimg v2" firmware (WRGG03 header,
 *        AES-256-CBC payload), as found on DAP-2xxx access points.
 *
 * Layout: a plain 0xA0-byte WRGG03 header (OpenWrt mkwrggimg.c) followed by
 * fsize bytes of AES-256-CBC ciphertext.
 *
 *   +0x00  char  signature[32]   e.g. "wapnd15_dlink_dap2695", NUL padded
 *   +0x20  u32   magic1          0x20080321
 *   +0x24  u32   magic2          0x20080321
 *   +0x28  char  version[16]
 *   +0x38  char  model[16]
 *   +0x48  u32   flag[2]
 *   +0x50  u32   reserve[2]
 *   +0x58  char  buildno[16]
 *   +0x68  u32   fsize           payload size, a multiple of 16
 *   +0x6C  u32   offset
 *   +0x70  char  devname[32]
 *   +0x90  u8    digest[16]
 *
 * Both byte orders exist.  The magic is stored in the image's byte order
 * (both copies alike); fsize is decoded both ways and the smaller value is
 * taken, as unblob's alpha_encimg_v2 handler does.
 *
 * The AES key and IV are a fixed pair ("EncParamsV2") mangled with the
 * signature string: k[i] ^ ((i + 1) % 0xFC) ^ signature[i % strlen].  The
 * reader decrypts the payload and publishes it as one member
 * "<signature>.bin".  Images whose first payload block is plainly not
 * ciphertext (a known plaintext magic, or under 3 bits of entropy over the
 * 16 bytes) are refused, like unblob does: those are unencrypted WRGG03
 * images, not this format.
 */

#ifndef XXFCLIB_FORMAT_D_LINK_ALPHA_ENCIMG_V2_H
#define XXFCLIB_FORMAT_D_LINK_ALPHA_ENCIMG_V2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_D_LINK_ALPHA_ENCIMG_V2_HEADER_SIZE 0xA0U
#define XX_D_LINK_ALPHA_ENCIMG_V2_MAGIC UINT32_C(0x20080321)
#define XX_D_LINK_ALPHA_ENCIMG_V2_SIGNATURE_SIZE 32U

typedef struct xx_d_link_alpha_encimg_v2 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t payload_size;   /**< fsize: ciphertext bytes after the header. */
    uint32_t offset_field;   /**< The header's "offset" word, informational. */
    bool header_big_endian;  /**< Byte order the magic was stored in. */
    char signature[XX_D_LINK_ALPHA_ENCIMG_V2_SIGNATURE_SIZE + 1U];
    char version[17];
    char model[17];
    char buildno[17];
    char devname[33];
} xx_d_link_alpha_encimg_v2;

typedef xx_d_link_alpha_encimg_v2 xx_d_link_alpha_encimg_v2_t;

XXFC_API void xx_d_link_alpha_encimg_v2_init(xx_d_link_alpha_encimg_v2 *archive,
                                             xx_io_device *device,
                                             int64_t base_address);
XXFC_API xx_d_link_alpha_encimg_v2 *xx_d_link_alpha_encimg_v2_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_d_link_alpha_encimg_v2_destroy(
    xx_d_link_alpha_encimg_v2 *archive);
XXFC_API void xx_d_link_alpha_encimg_v2_free(xx_d_link_alpha_encimg_v2 *archive);

XXFC_API bool xx_d_link_alpha_encimg_v2_check_is_valid(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API bool xx_d_link_alpha_encimg_v2_handle_base_info(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API int64_t xx_d_link_alpha_encimg_v2_get_format_size(Abstractformat *self,
                                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_d_link_alpha_encimg_v2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_d_link_alpha_encimg_v2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_d_link_alpha_encimg_v2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_d_link_alpha_encimg_v2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_d_link_alpha_encimg_v2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_d_link_alpha_encimg_v2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_D_LINK_ALPHA_ENCIMG_V2_H */
