/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_ghost_installer.h
 * @brief Ghost Installer (Ethalone) package (.gip) and setup.exe payload.
 *
 * A Ghost Installer package is a chain of segments.  Each segment is an
 * ordinary Microsoft Cabinet whose every byte has been XORed with 0x8D,
 * followed by a clear (not XORed) trailer that ends in "GIPEND":
 *
 *   segment  := cabinet ^ 0x8D, trailer
 *   cabinet  := "MSCF" 00 00 00 00 ... (cbCabinet bytes, CFHEADER.cbCabinet)
 *   trailer  := 34 bytes, "GIPEND" at +28, or
 *               26 bytes, "GIPEND" at +20
 *
 * so every package starts with the masked cabinet signature
 * C0 DE CE CB 8D 8D 8D 8D.  In the 34-byte trailer observed in the corpus
 * +0 is the package start (0), +4 the package end (the file size), +8 is 1,
 * +12 the size of the first member (db.pdb, the installer database) and +20
 * the total size of the remaining members; none of those are needed to read
 * the package and they are not trusted.  A segment without a trailer ends
 * the chain.  The first member of each cabinet is db.pdb; the installed
 * files follow as data\{GUID}\<n>\<name>.  The corpus packages use LZX:21.
 *
 * The same package is appended to the Ghost Installer setup.exe stub: it
 * then starts at the PE overlay (the end of the last section's raw data).
 * The executable is parsed only to find that offset; no code is run.
 */

#ifndef XXFCLIB_FORMAT_GHOST_INSTALLER_H
#define XXFCLIB_FORMAT_GHOST_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Masked cabinet signature at the start of every segment. */
#define XX_GHOST_INSTALLER_MAGIC "\xC0\xDE\xCE\xCB\x8D\x8D\x8D\x8D"
#define XX_GHOST_INSTALLER_MAGIC_SIZE 8U
/** Single-byte XOR mask of the cabinet bytes. */
#define XX_GHOST_INSTALLER_KEY 0x8DU
/** The two trailer sizes and the marker that closes them. */
#define XX_GHOST_INSTALLER_TRAILER_LONG 34U
#define XX_GHOST_INSTALLER_TRAILER_SHORT 26U
#define XX_GHOST_INSTALLER_TRAILER_MARKER "GIPEND"
/** Upper bound on segments followed in one package. */
#define XX_GHOST_INSTALLER_MAX_SEGMENTS 64U

typedef struct xx_ghost_installer {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset; /**< Absolute offset of the first cabinet. */
    int64_t payload_end;    /**< Absolute end of the last segment. */
    uint32_t segment_count;
    uint32_t first_trailer; /**< 34, 26, or 0 when the first trailer is absent. */
    bool is_sfx;            /**< Payload found behind a PE image. */
} xx_ghost_installer;

typedef xx_ghost_installer xx_ghost_installer_t;

XXFC_API void xx_ghost_installer_init(xx_ghost_installer *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_ghost_installer *xx_ghost_installer_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_ghost_installer_destroy(xx_ghost_installer *archive);
XXFC_API void xx_ghost_installer_free(xx_ghost_installer *archive);

XXFC_API bool xx_ghost_installer_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_ghost_installer_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_ghost_installer_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_ghost_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ghost_installer_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ghost_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ghost_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ghost_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ghost_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the first cabinet, or -1 before a successful parse. */
XXFC_API int64_t xx_ghost_installer_get_payload_offset(
    const xx_ghost_installer *archive);
/** Number of cabinet segments, 0 before a successful parse. */
XXFC_API uint32_t xx_ghost_installer_get_segment_count(
    const xx_ghost_installer *archive);
/** True when the package sits behind a PE image (setup.exe). */
XXFC_API bool xx_ghost_installer_is_sfx(const xx_ghost_installer *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GHOST_INSTALLER_H */
