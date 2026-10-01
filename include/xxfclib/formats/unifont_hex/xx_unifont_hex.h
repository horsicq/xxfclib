/* SPDX-License-Identifier: MIT
 * Primary reference: https://unifoundry.com/unifont/index.html
 * GNU Unifont HEX: complete ascending unique Unicode scalar codepoint records with exact8x16 or16x16 monochrome bitmap hex data; original natural256-codepoint-page components exported; unsupported glyph dimensions/comments/dialects declined. Signatureless fallback after structured readers.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_UNIFONT_HEX_H
#define XX_UNIFONT_HEX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unifont_hex {Abstractformat format;} xx_unifont_hex;
XXFC_API void xx_unifont_hex_init(xx_unifont_hex *,xx_io_device *,int64_t);
XXFC_API xx_unifont_hex *xx_unifont_hex_create(xx_io_device *,int64_t);
XXFC_API void xx_unifont_hex_destroy(xx_unifont_hex *);
XXFC_API void xx_unifont_hex_free(xx_unifont_hex *);
XXFC_API bool xx_unifont_hex_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_unifont_hex_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
