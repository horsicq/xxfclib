/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/1j01/anypalette.js/master/anypalette.js
 * Adobe Color Table: exact768-byte RGB256 table or772-byte counted/transparency variant, bounded active color count and transparent index. Original color triplets and optional descriptor exported; no color-space conversion. Signatureless fixed-size palette recognition is an ambiguous fallback after structured readers.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_ADOBE_ACT_H
#define XX_ADOBE_ACT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_act {Abstractformat format;} xx_adobe_act;
XXFC_API void xx_adobe_act_init(xx_adobe_act *,xx_io_device *,int64_t);
XXFC_API xx_adobe_act *xx_adobe_act_create(xx_io_device *,int64_t);
XXFC_API void xx_adobe_act_destroy(xx_adobe_act *);
XXFC_API void xx_adobe_act_free(xx_adobe_act *);
XXFC_API bool xx_adobe_act_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adobe_act_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
