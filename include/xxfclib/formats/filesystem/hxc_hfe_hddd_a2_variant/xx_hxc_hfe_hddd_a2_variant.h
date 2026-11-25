/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_HXC_HFE_HDDD_A2_VARIANT_H
#define XXFCLIB_FORMAT_HXC_HFE_HDDD_A2_VARIANT_H

#include "xxfclib/formats/xx_format.h"

/* HxC Floppy Emulator HFE written in "HDDD A2" mode: the bit cells are stored
 * at twice the rate with every source cell pair expanded to a fixed four-cell
 * group.  Members: the track data folded back to a standard-rate HFE, plus a
 * DOS-order Apple II image when 6-and-2 GCR sectors decode. */
typedef struct xx_hxc_hfe_hddd_a2_variant {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_hxc_hfe_hddd_a2_variant;

XXFC_API void xx_hxc_hfe_hddd_a2_variant_init(xx_hxc_hfe_hddd_a2_variant *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_hxc_hfe_hddd_a2_variant *xx_hxc_hfe_hddd_a2_variant_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_hxc_hfe_hddd_a2_variant_destroy(xx_hxc_hfe_hddd_a2_variant *archive);
XXFC_API void xx_hxc_hfe_hddd_a2_variant_free(xx_hxc_hfe_hddd_a2_variant *archive);
XXFC_API bool xx_hxc_hfe_hddd_a2_variant_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hxc_hfe_hddd_a2_variant_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hxc_hfe_hddd_a2_variant_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_hxc_hfe_hddd_a2_variant_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_hxc_hfe_hddd_a2_variant_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hxc_hfe_hddd_a2_variant_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hxc_hfe_hddd_a2_variant_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hxc_hfe_hddd_a2_variant_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hxc_hfe_hddd_a2_variant_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#endif
