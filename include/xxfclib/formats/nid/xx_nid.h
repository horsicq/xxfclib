/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nid.h @brief "NI" install-set (.nid) archive reader. */

#ifndef XXFCLIB_FORMAT_NID_H
#define XXFCLIB_FORMAT_NID_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_nid xx_nid;
typedef struct xx_nid xx_nid_t;
typedef struct xx_nid XNid;

/**
 * @brief An "NI" install-set volume (DISK001.NID, RAID001.DAT, Z.PAC).
 *
 * A DOS-era installer data volume: a 0x78-byte header, a flat directory of
 * 29-byte entries, and per-member chains of five-byte-framed blocks.  Members
 * are grouped into output folders by an opaque folder key, numbered
 * "Folder1", "Folder2", ... in the order the keys are first seen.
 */
struct xx_nid {
    Abstractformat format;
    uint64_t number_of_records; /**< Members listed (may stop before the count). */
    uint32_t number_of_entries; /**< Directory entry count from the header. */
    int64_t directory_offset;   /**< Always 0x78 relative to the base address. */
    int64_t directory_size;     /**< number_of_entries * 29. */
    int64_t archive_size;       /**< End of the furthest member block chain. */
};

XXFC_API void xx_nid_init(xx_nid *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_nid *xx_nid_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nid_destroy(xx_nid *archive);
XXFC_API void xx_nid_free(xx_nid *archive);

XXFC_API bool xx_nid_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nid_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nid_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_nid_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_nid_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nid_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nid_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nid_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nid_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_nid_get_number_of_records(const xx_nid *archive);
XXFC_API int64_t xx_nid_get_archive_size(const xx_nid *archive);

static inline Abstractformat *xx_nid_to_format(xx_nid *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NID_H */
