/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_TRS_80_JV3_H
#define XXFCLIB_FORMAT_TRS_80_JV3_H

#include "xxfclib/formats/xx_format.h"

/* TRS-80 JV3 floppy image (Jeff Vavasour's format, as used by xtrs).
 * Blocks of 2901 three-byte sector headers (cylinder, sector, flags) plus a
 * write-protect byte, each followed by the data of those sectors in header
 * order.  One member per physical track (cylinder, side), its sectors in
 * sector-ID order. */
typedef struct xx_trs_80_jv3 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_sectors;
    int64_t archive_end;
    bool write_protected;
} xx_trs_80_jv3;

XXFC_API void xx_trs_80_jv3_init(xx_trs_80_jv3 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_trs_80_jv3 *xx_trs_80_jv3_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_trs_80_jv3_destroy(xx_trs_80_jv3 *archive);
XXFC_API void xx_trs_80_jv3_free(xx_trs_80_jv3 *archive);
XXFC_API bool xx_trs_80_jv3_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_trs_80_jv3_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_trs_80_jv3_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_trs_80_jv3_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_trs_80_jv3_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_trs_80_jv3_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_trs_80_jv3_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_trs_80_jv3_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_trs_80_jv3_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_trs_80_jv3_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_trs_80_jv3_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_trs_80_jv3_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_trs_80_jv3_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_trs_80_jv3_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
