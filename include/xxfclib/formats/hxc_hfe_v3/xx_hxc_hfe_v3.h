/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_HXC_HFE_V3_H
#define XXFCLIB_FORMAT_HXC_HFE_V3_H

#include "xxfclib/formats/xx_format.h"

/* HxC Floppy Emulator HFE v3 image ("HXCHFEV3").  Bit cells per cylinder with
 * in-stream opcodes; the member is the decoded flat sector image, or one raw
 * bit-cell stream per track side when no sector layout is recognised. */
typedef struct xx_hxc_hfe_v3 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_hxc_hfe_v3;

XXFC_API void xx_hxc_hfe_v3_init(xx_hxc_hfe_v3 *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_hxc_hfe_v3 *xx_hxc_hfe_v3_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_hxc_hfe_v3_destroy(xx_hxc_hfe_v3 *archive);
XXFC_API void xx_hxc_hfe_v3_free(xx_hxc_hfe_v3 *archive);
XXFC_API bool xx_hxc_hfe_v3_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_hxc_hfe_v3_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_hxc_hfe_v3_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_hxc_hfe_v3_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_hxc_hfe_v3_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hxc_hfe_v3_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hxc_hfe_v3_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hxc_hfe_v3_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hxc_hfe_v3_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
