/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/NeuralEnsemble/python-neo/blob/master/neo/rawio/axonarawio.py */
#ifndef XX_AXONA_TETRODE_H
#define XX_AXONA_TETRODE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_axona_tetrode { Abstractformat format; } xx_axona_tetrode;
XXFC_API void xx_axona_tetrode_init(xx_axona_tetrode *,xx_io_device *,int64_t);
XXFC_API xx_axona_tetrode *xx_axona_tetrode_create(xx_io_device *,int64_t);
XXFC_API void xx_axona_tetrode_destroy(xx_axona_tetrode *);
XXFC_API void xx_axona_tetrode_free(xx_axona_tetrode *);
XXFC_API bool xx_axona_tetrode_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_axona_tetrode_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
