/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_eschalon_setup_epsf.h
 *  @brief Eschalon Setup 3 "EPSF" self-extractor reader.
 *
 * The carrier is a 32-bit Delphi PE stub.  Everything the installer needs
 * sits in the PE overlay, which starts with an 18-byte header:
 *
 *     +0x00  char[4]  "EPSF"
 *     +0x04  u16      version, 3 on every known build
 *     +0x06  u32      unpacked size of the installer runtime (SETUPMN.DLL)
 *     +0x0A  u32      packed size of the installer runtime
 *     +0x0E  u32      32-bit sum of the runtime's unpacked bytes
 *
 * and is followed by three consecutive regions:
 *
 *   1. SETUPMN.DLL, packed with the ARCV 4.00 method-2 codec, exactly the
 *      declared packed size long;
 *   2. the setup project (a Delphi "TPF0" object stream) in the same codec.
 *      Nothing records its size; the stream ends at the codec's end symbol;
 *   3. on carriers that ship a product, a complete ARCV 4.00 container
 *      ("ARCV" 00 04), read through the existing ARCV4 reader.
 *
 * Members: SETUPMN.DLL, SETUP_SCRIPT.DFM (when the script decodes), then
 * every ARCV4 member under the name the ARCV4 reader gives it.
 */

#ifndef XXFCLIB_FORMAT_ESCHALON_SETUP_EPSF_H
#define XXFCLIB_FORMAT_ESCHALON_SETUP_EPSF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_eschalon_setup_epsf {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t header_offset;        /**< EPSF header (the PE overlay), absolute. */
    uint32_t runtime_size;        /**< Unpacked SETUPMN.DLL size. */
    uint32_t runtime_packed_size; /**< Packed SETUPMN.DLL size. */
    uint32_t runtime_checksum;    /**< 32-bit sum of the unpacked runtime. */
    int64_t script_offset;        /**< Setup script stream, absolute. */
    int64_t script_region_size;   /**< Bytes up to the archive (or end). */
    uint64_t script_size;         /**< Decoded script size; 0 if undecodable. */
    int64_t archive_offset;       /**< ARCV 4.00 container, absolute; -1 if none. */
    int64_t archive_size;         /**< Size of that container; 0 if none. */
    uint64_t archive_records;     /**< Members of that container. */
} xx_eschalon_setup_epsf;

typedef xx_eschalon_setup_epsf xx_eschalon_setup_epsf_t;

XXFC_API void xx_eschalon_setup_epsf_init(xx_eschalon_setup_epsf *archive,
                                          xx_io_device *device,
                                          int64_t base_address);
XXFC_API xx_eschalon_setup_epsf *xx_eschalon_setup_epsf_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_eschalon_setup_epsf_destroy(xx_eschalon_setup_epsf *archive);
XXFC_API void xx_eschalon_setup_epsf_free(xx_eschalon_setup_epsf *archive);

XXFC_API bool xx_eschalon_setup_epsf_check_is_valid(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API bool xx_eschalon_setup_epsf_handle_base_info(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API int64_t xx_eschalon_setup_epsf_get_format_size(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_eschalon_setup_epsf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_eschalon_setup_epsf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_eschalon_setup_epsf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_eschalon_setup_epsf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_eschalon_setup_epsf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_eschalon_setup_epsf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ESCHALON_SETUP_EPSF_H */
