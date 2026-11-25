/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/coreprime/kbot-io/blob/main/formats/hpi/v1/reader.go
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_TOTALANNIHILATION_HPI_H
#define XX_TOTALANNIHILATION_HPI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_totalannihilation_hpi {
    Abstractformat format;
} xx_totalannihilation_hpi;
XXFC_API void xx_totalannihilation_hpi_init(xx_totalannihilation_hpi *, xx_io_device *, int64_t);
XXFC_API xx_totalannihilation_hpi *xx_totalannihilation_hpi_create(xx_io_device *, int64_t);
XXFC_API void xx_totalannihilation_hpi_destroy(xx_totalannihilation_hpi *);
XXFC_API void xx_totalannihilation_hpi_free(xx_totalannihilation_hpi *);
XXFC_API bool xx_totalannihilation_hpi_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_totalannihilation_hpi_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_totalannihilation_hpi_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_totalannihilation_hpi_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_totalannihilation_hpi_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_totalannihilation_hpi_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_totalannihilation_hpi_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
