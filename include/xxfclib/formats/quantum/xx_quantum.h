/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_quantum.h
 * @brief Standalone Quantum archive (.pak / .001) container reader.
 *
 * This is David Stafford's Q.EXE archive, NOT the CAB-embedded Quantum codec.
 * The two share an LZ+arithmetic design but not a stream shape; see the notes
 * in xx_quantum.c for exactly which archives this reader can extract.
 */

#ifndef XXFCLIB_FORMAT_QUANTUM_H
#define XXFCLIB_FORMAT_QUANTUM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A standalone Quantum archive.
 *
 * An 8-byte header, then one variable-length directory record per member, then
 * ONE solid arithmetic-coded stream running to end of file.  Models, LZ window
 * and coder registers run continuously across the members, so every record
 * publishes the whole body as its stream and a member can only be produced by
 * replaying the members in front of it.
 */
typedef struct xx_quantum {
    Abstractformat format;      /**< Must stay first: the reader casts to it. */
    uint64_t number_of_records; /**< Members listed in the directory. */
    int64_t stream_offset;      /**< First byte of the solid stream. */
    int64_t stream_size;        /**< Solid stream length, to end of file. */
    uint32_t window_bits;       /**< LZ window order, 10..21. */
    uint32_t version;           /**< Header byte 3; < 0x17 is the old shape. */
    uint32_t level;             /**< Packer effort; does not affect decoding. */
    bool old_variant;           /**< version < 0x17. */
} xx_quantum;

typedef xx_quantum xx_quantum_t;

XXFC_API void xx_quantum_init(xx_quantum *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_quantum *xx_quantum_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_quantum_destroy(xx_quantum *archive);
XXFC_API void xx_quantum_free(xx_quantum *archive);

XXFC_API bool xx_quantum_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_quantum_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_quantum_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_quantum_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_quantum_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_quantum_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_quantum_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_quantum_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_quantum_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_quantum_get_number_of_records(const xx_quantum *archive);
XXFC_API uint32_t xx_quantum_get_window_bits(const xx_quantum *archive);
XXFC_API bool xx_quantum_get_old_variant(const xx_quantum *archive);

static inline Abstractformat *xx_quantum_to_format(xx_quantum *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QUANTUM_H */
