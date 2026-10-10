/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_IS_STORED_H
#define XXFCLIB_FORMAT_IS_STORED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Atari stored wrapper using fixed XOR A9. */
typedef struct xx_is_stored {
    Abstractformat format;
} xx_is_stored;
XXFC_API void xx_is_stored_init(xx_is_stored *, xx_io_device *, int64_t);
XXFC_API xx_is_stored *xx_is_stored_create(xx_io_device *, int64_t);
XXFC_API void xx_is_stored_destroy(xx_is_stored *);
XXFC_API void xx_is_stored_free(xx_is_stored *);
XXFC_API bool xx_is_stored_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_is_stored_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_is_stored_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
