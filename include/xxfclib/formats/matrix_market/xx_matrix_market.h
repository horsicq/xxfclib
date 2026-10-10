/* SPDX-License-Identifier: MIT
 * Wire specification: https://math.nist.gov/MatrixMarket/formats.html */
#ifndef XX_MATRIX_MARKET_H
#define XX_MATRIX_MARKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_matrix_market {
    Abstractformat format;
} xx_matrix_market;
XXFC_API void xx_matrix_market_init(xx_matrix_market *, xx_io_device *, int64_t);
XXFC_API xx_matrix_market *xx_matrix_market_create(xx_io_device *, int64_t);
XXFC_API void xx_matrix_market_destroy(xx_matrix_market *);
XXFC_API void xx_matrix_market_free(xx_matrix_market *);
XXFC_API bool xx_matrix_market_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_matrix_market_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_matrix_market_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_matrix_market_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_matrix_market_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_matrix_market_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_matrix_market_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
