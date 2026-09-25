/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wise_installation_system.h
 *  @brief Wise Installation System installers (Wise16 NE / Wise32 PE). */

#ifndef XXFCLIB_FORMAT_WISE_INSTALLATION_SYSTEM_H
#define XXFCLIB_FORMAT_WISE_INSTALLATION_SYSTEM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Wise installer: a small NE or PE stub (it imports "WiseMain"
 * from WISE0001.DLL) followed by the installation data in its overlay.
 *
 * The overlay opens with a version-dependent Wise header (0x1E, 0x22,
 * 0x40 or 0x51 bytes, or 0x5C bytes plus a length-prefixed block of dialog
 * font strings), and then holds one of two member chains:
 *
 *  - native: raw-DEFLATE streams, each followed by the CRC-32 of its
 *    output (occasionally after 1..3 zero bytes).  The first members are
 *    the installer's own (colour DIB, the WISE script, WISE0001.DLL, ...);
 *    the installed files follow.  The streams carry no names: the script's
 *    file records hold each file's (start, end) offsets relative to the
 *    first installed stream, followed by its destination path such as
 *    "%MAINDIR%\\README.TXT", which becomes "MAINDIR/README.TXT".
 *  - PK mode: every stream wrapped in a ZIP local header (flag bit 15 set)
 *    with a central directory that uses absolute file offsets and does not
 *    index the first (unnamed) member.
 *
 * The executable is parsed only far enough to find where its image ends;
 * nothing is executed or emulated.
 */
typedef struct xx_wise_installation_system {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t overlay_offset;  /**< End of the NE/PE image (device offset). */
    int64_t payload_offset;  /**< First member stream or local header. */
    int64_t chain_end;       /**< End of the last complete member. */
    uint64_t named_records;  /**< Members named by the script / headers. */
    bool is_ne;              /**< Wise16 NE stub (else Wise32 PE). */
    bool pk_mode;            /**< ZIP-framed member chain. */
    bool truncated;          /**< Chain stops before the end of the file. */
    void *cache;             /**< Parsed member table (owned, refcounted). */
} xx_wise_installation_system;

typedef xx_wise_installation_system xx_wise_installation_system_t;

XXFC_API void xx_wise_installation_system_init(
    xx_wise_installation_system *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_wise_installation_system *xx_wise_installation_system_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_wise_installation_system_destroy(
    xx_wise_installation_system *archive);
XXFC_API void xx_wise_installation_system_free(
    xx_wise_installation_system *archive);

XXFC_API bool xx_wise_installation_system_check_is_valid(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API bool xx_wise_installation_system_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_wise_installation_system_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_wise_installation_system_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_wise_installation_system_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_wise_installation_system_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wise_installation_system_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wise_installation_system_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wise_installation_system_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode member @p index into @p destination (NULL only verifies).
 *
 * Succeeds only when the member decodes to its recorded size and CRC-32.
 */
XXFC_API bool xx_wise_installation_system_unpack_record_to_device(
    xx_wise_installation_system *archive, uint64_t index,
    xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WISE_INSTALLATION_SYSTEM_H */
