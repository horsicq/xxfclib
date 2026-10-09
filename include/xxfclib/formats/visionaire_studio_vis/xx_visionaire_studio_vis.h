/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_visionaire_studio_vis.h @brief Visionaire Studio VIS3 archive reader. */

#ifndef XXFCLIB_FORMAT_VISIONAIRE_STUDIO_VIS_H
#define XXFCLIB_FORMAT_VISIONAIRE_STUDIO_VIS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Visionaire Studio 3.x+ game data archive (".vis", "VIS3").
 *
 *   0x00  char[4]  "VIS3"
 *   0x04  u32      member count N.  Big-endian unless the little-endian
 *                  reading is the smaller value; that choice fixes the byte
 *                  order of every index field.
 *   0x08  index, 3 + 16 * N + 3 bytes, XORed with a 16-character ASCII key
 *         (the hex form of 8 bytes of an MD5; key index 0 at 0x08):
 *           "HDR"
 *           N * { u32 offset, u32 stored size, u32 size, u32 flags }
 *           "END"
 *   data  members back to back, offsets relative to the end of the index;
 *         the first starts at 0 and each follows the previous one.
 *
 * Member flags:
 *   0x02  stored bytes (or, for chunked members, each chunk's zlib bytes) are
 *         XORed with the key, key index 0 at their first byte
 *   0x08  image obfuscation (PNG/WebP, key derived from the member name,
 *         which the archive does not store); such members are extracted as
 *         stored and reported encrypted
 *   0x10  with stored != size: a chunk sequence
 *           { u32 BE size, u32 BE stored, zlib[stored] }...
 *         optionally closed by a chunk whose stored field is 0xFFEEFFEE
 *   otherwise stored != size means one zlib stream (RFC 1950).
 *
 * The key is taken from a list of known game keys, or else derived from the
 * known plaintext ("HDR", a zero first offset, contiguous offsets, and one
 * stored member whose two size fields agree) the way VIS3Ext does.  A key is
 * accepted only when the whole index decrypts to "HDR" ... "END" with
 * contiguous members inside the file.
 *
 * The archive carries no member names; members are named
 * "<index %05u>_<offset %08x>.<ext>" like VIS3Ext's default output (ext is
 * "xml" for flags 0x10, "png" for flags 0x08, else "dat").  The older
 * "VIS" (non-VIS3) layout and archives appended to game executables are not
 * handled.
 */
typedef struct xx_visionaire_studio_vis {
    Abstractformat format;
    uint64_t number_of_records;
    bool big_endian;
    char key[17]; /**< The resolved index key, NUL terminated. */
} xx_visionaire_studio_vis;

typedef xx_visionaire_studio_vis xx_visionaire_studio_vis_t;

XXFC_API void xx_visionaire_studio_vis_init(xx_visionaire_studio_vis *archive,
                                            xx_io_device *device,
                                            int64_t base_address);
XXFC_API xx_visionaire_studio_vis *xx_visionaire_studio_vis_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_visionaire_studio_vis_destroy(
    xx_visionaire_studio_vis *archive);
XXFC_API void xx_visionaire_studio_vis_free(xx_visionaire_studio_vis *archive);

XXFC_API bool xx_visionaire_studio_vis_check_is_valid(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API bool xx_visionaire_studio_vis_handle_base_info(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API int64_t xx_visionaire_studio_vis_get_format_size(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_visionaire_studio_vis_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_visionaire_studio_vis_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_visionaire_studio_vis_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_visionaire_studio_vis_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_visionaire_studio_vis_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_visionaire_studio_vis_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_visionaire_studio_vis_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_visionaire_studio_vis_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_visionaire_studio_vis_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_visionaire_studio_vis_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_visionaire_studio_vis_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_VISIONAIRE_STUDIO_VIS_H */
