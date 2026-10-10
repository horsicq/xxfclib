/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT */
#ifndef XX_INSTALIT_DATA_H
#define XX_INSTALIT_DATA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicitly selected footerless Instalit media reader. No global DCL
 * detection priority is added. Only complete independent prefix streams are
 * members; a partial stream or manifest tail is retained in format_size. */
typedef struct xx_instalit_data_info {
    uint64_t complete_streams;
    int64_t data_end; /* absolute device offset after complete streams */
    int64_t trailing_size;
    int64_t manifest_offset;   /* absolute first name marker, or -1 */
    int64_t manifest_name_end; /* absolute end of the final recovered name */
    uint32_t manifest_stride;  /* 55 or 59, zero when no candidate run exists */
    uint32_t manifest_run;     /* number of names in the longest candidate run */
    uint64_t named_streams;    /* zero unless the run covers every stream */
} xx_instalit_data_info;
typedef struct xx_instalit_data {
    Abstractformat format;
    xx_instalit_data_info info;
} xx_instalit_data;
XXFC_API void xx_instalit_data_init(xx_instalit_data *, xx_io_device *, int64_t);
XXFC_API xx_instalit_data *xx_instalit_data_create(xx_io_device *, int64_t);
XXFC_API void xx_instalit_data_destroy(xx_instalit_data *);
XXFC_API void xx_instalit_data_free(xx_instalit_data *);
XXFC_API bool xx_instalit_data_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_instalit_data_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_instalit_data_get_info(xx_instalit_data *, xx_instalit_data_info *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_instalit_data_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
