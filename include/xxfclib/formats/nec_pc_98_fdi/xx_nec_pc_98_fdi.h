/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_NEC_PC_98_FDI_H
#define XXFCLIB_FORMAT_NEC_PC_98_FDI_H

#include "xxfclib/formats/xx_format.h"

/* NEC PC-98 FDI floppy image (Anex86): a header of eight little-endian
 * 32-bit fields, padded to its declared size (normally 4096 bytes), then
 * the stored sectors in plain cylinder/head/sector order.  Not the ZX
 * Spectrum "FDI" format, which the fdi reader handles. */
typedef struct xx_nec_pc_98_fdi {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t fdd_type;
    uint32_t header_size;
    uint32_t heads;
    uint32_t cylinders;
    uint32_t sectors_per_track;
    uint32_t sector_size;
    uint64_t image_size;
} xx_nec_pc_98_fdi;

XXFC_API void xx_nec_pc_98_fdi_init(xx_nec_pc_98_fdi *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_nec_pc_98_fdi *xx_nec_pc_98_fdi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nec_pc_98_fdi_destroy(xx_nec_pc_98_fdi *archive);
XXFC_API void xx_nec_pc_98_fdi_free(xx_nec_pc_98_fdi *archive);
XXFC_API bool xx_nec_pc_98_fdi_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nec_pc_98_fdi_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nec_pc_98_fdi_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_nec_pc_98_fdi_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_nec_pc_98_fdi_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nec_pc_98_fdi_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nec_pc_98_fdi_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nec_pc_98_fdi_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nec_pc_98_fdi_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nec_pc_98_fdi_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nec_pc_98_fdi_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nec_pc_98_fdi_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nec_pc_98_fdi_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nec_pc_98_fdi_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
