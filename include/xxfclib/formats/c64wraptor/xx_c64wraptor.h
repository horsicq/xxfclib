/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_c64wraptor.h @brief Commodore 64 Wraptor (*.wra, *.wr3) reader. */

#ifndef XXFCLIB_FORMAT_C64WRAPTOR_H
#define XXFCLIB_FORMAT_C64WRAPTOR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A C64 Wraptor archive: a chain of members, each a four-byte magic, a
 *        NUL-terminated name, one flags byte and an LZSS stream followed by a
 *        two-byte checksum. The chain ends at an explicit end marker.
 *
 * The container stores NEITHER the packed nor the unpacked length of a member,
 * so both are recovered by walking the stream with the codec's scan entry
 * point; listing an archive therefore costs a full decode pass.
 */
typedef struct xx_c64wraptor {
    Abstractformat format;
    uint64_t number_of_records;
} xx_c64wraptor;

typedef xx_c64wraptor xx_c64wraptor_t;

XXFC_API void xx_c64wraptor_init(xx_c64wraptor *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_c64wraptor *xx_c64wraptor_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_c64wraptor_destroy(xx_c64wraptor *archive);
XXFC_API void xx_c64wraptor_free(xx_c64wraptor *archive);

XXFC_API bool xx_c64wraptor_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_c64wraptor_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_c64wraptor_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_c64wraptor_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_c64wraptor_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_c64wraptor_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_c64wraptor_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_c64wraptor_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_c64wraptor_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_C64WRAPTOR_H */
