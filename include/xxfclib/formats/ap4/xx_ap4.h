/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ap4.h @brief AP4 audio container reader. */

#ifndef XXFCLIB_FORMAT_AP4_H
#define XXFCLIB_FORMAT_AP4_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An AP4 audio container.
 *
 * AP4 is the container used by a family of hardware reading pens. It has no
 * magic number anywhere: what identifies it is a chain of small tables of
 * contents near the start of the file, each listing member extents, followed
 * by the members themselves -- and the requirement that the first member that
 * is not entirely zero decodes as MPEG audio.
 *
 * Members are stored verbatim or XOR-obfuscated with a single repeating byte.
 * The key is recovered per member from the assumption that the plaintext
 * begins with an MPEG sync byte, and confirmed by decoding a second frame
 * header at the distance the first frame's own fields predict.
 *
 * Member names are not stored. They are synthesised from each member's extent
 * the way the reference tool does: "<stem> 0xSTART-0xEND (SIZE).mp3" for audio
 * members and ".unk" for the rest. The stem is "ap4", which is what the
 * reference falls back to when the container's own file name is unavailable --
 * it is here too, since an xx_io_device need not be a file.
 */
typedef struct xx_ap4 {
    Abstractformat format;
    uint64_t number_of_records; /**< Members kept after classification. */
    int64_t prefix_size;        /**< Offset of the first non-empty TOC. */
    int64_t header_size;        /**< End of the last TOC. */
    uint32_t toc_count;         /**< Tables of contents in the chain. */
} xx_ap4;

typedef xx_ap4 xx_ap4_t;
typedef xx_ap4 XAp4;

XXFC_API void xx_ap4_init(xx_ap4 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_ap4 *xx_ap4_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ap4_destroy(xx_ap4 *archive);
XXFC_API void xx_ap4_free(xx_ap4 *archive);

XXFC_API bool xx_ap4_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ap4_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ap4_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_ap4_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ap4_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ap4_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ap4_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ap4_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ap4_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief Offset of the first non-empty table of contents. */
XXFC_API int64_t xx_ap4_get_prefix_size(const xx_ap4 *archive);
/** @brief Number of tables of contents in the chain. */
XXFC_API uint32_t xx_ap4_get_toc_count(const xx_ap4 *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AP4_H */
