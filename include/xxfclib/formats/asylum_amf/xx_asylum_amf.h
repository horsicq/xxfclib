/* SPDX-License-Identifier: MIT */
#ifndef XX_ASYLUM_AMF_H
#define XX_ASYLUM_AMF_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_asylum_amf {Abstractformat format;} xx_asylum_amf;
XXFC_API void xx_asylum_amf_init(xx_asylum_amf *,xx_io_device *,int64_t);
XXFC_API xx_asylum_amf *xx_asylum_amf_create(xx_io_device *,int64_t);
XXFC_API void xx_asylum_amf_destroy(xx_asylum_amf *);
XXFC_API void xx_asylum_amf_free(xx_asylum_amf *);
XXFC_API bool xx_asylum_amf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_asylum_amf_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
