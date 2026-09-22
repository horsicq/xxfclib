/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_MACBINARY_H
#define XXFCLIB_FORMAT_MACBINARY_H

#include "xxfclib/formats/xx_format.h"

/* MacBinary I / II / III: a 128-byte header followed by the data fork and the
 * resource fork, each padded to a 128-byte boundary. */
typedef struct xx_macbinary {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t version;         /* 0 = MacBinary I, 129 = II, 130 = III */
    bool has_signature;       /* "mBIN" at offset 102 (MacBinary III) */
    bool crc_verified;        /* header CRC-16 present and correct */
} xx_macbinary;

XXFC_API void xx_macbinary_init(xx_macbinary *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_macbinary *xx_macbinary_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_macbinary_destroy(xx_macbinary *archive);
XXFC_API void xx_macbinary_free(xx_macbinary *archive);
XXFC_API bool xx_macbinary_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_macbinary_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_macbinary_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_macbinary_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_macbinary_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_macbinary_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_macbinary_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_macbinary_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_macbinary_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
