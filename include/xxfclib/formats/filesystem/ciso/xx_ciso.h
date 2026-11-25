/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ciso.h @brief CISO v1/v2, ZISO and DAX compressed ISO images. */

#ifndef XXFCLIB_FORMAT_CISO_H
#define XXFCLIB_FORMAT_CISO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A CISO/ZISO image: a block index over an ISO. v1 has stored/Deflate
 * blocks; v2 has stored/Deflate/LZ4; ZISO has stored/LZ4. */
typedef struct xx_ciso {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    void *nested_iso;
} xx_ciso;

typedef struct xx_ciso xx_ciso_t;

XXFC_API void xx_ciso_init(xx_ciso *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_ciso *xx_ciso_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ciso_destroy(xx_ciso *archive);
XXFC_API void xx_ciso_free(xx_ciso *archive);
XXFC_API bool xx_ciso_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ciso_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ciso_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ciso_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_ciso_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ciso_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ciso_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ciso_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ciso_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_ciso_to_format(xx_ciso *archive)
{
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ciso_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ciso_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ciso_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ciso_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ciso_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_CISO_H */
