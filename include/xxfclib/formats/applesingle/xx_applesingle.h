/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_APPLESINGLE_H
#define XXFCLIB_FORMAT_APPLESINGLE_H

#include "xxfclib/formats/xx_format.h"

/* AppleSingle (0x00051600) and AppleDouble (0x00051607) wrappers.  Both use
 * the same big-endian header and entry descriptor table; AppleDouble simply
 * omits the data fork, which lives in the companion file. */
typedef struct xx_applesingle {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t magic;
    uint32_t version;
    uint32_t number_of_entries;
} xx_applesingle;

XXFC_API void xx_applesingle_init(xx_applesingle *archive, xx_io_device *device,
                                  int64_t base_address);
XXFC_API xx_applesingle *xx_applesingle_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_applesingle_destroy(xx_applesingle *archive);
XXFC_API void xx_applesingle_free(xx_applesingle *archive);
XXFC_API bool xx_applesingle_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_applesingle_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_applesingle_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_applesingle_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_applesingle_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_applesingle_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_applesingle_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_applesingle_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_applesingle_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
