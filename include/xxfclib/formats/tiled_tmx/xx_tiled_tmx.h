/* SPDX-License-Identifier: MIT
 * Primary reference: https://doc.mapeditor.org/en/stable/reference/tmx-map-format/
 * Tiled TMX1.x finite orthogonal maps: complete UTF8 XML without DTD or external entities, embedded rectangular image tilesets with declared tilecount/columns, image geometry and sorted nonoverlapping GID ranges, unique layer IDs and exact CSV or uncompressed strict-base64 matrix counts/GIDs. Original XML and decoded LE32 tile arrays exported. External image paths retained as metadata only. Infinite/chunked maps, external TSX, compressed data, property/object/group/custom tile sections and other orientations declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_TILED_TMX_H
#define XX_TILED_TMX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tiled_tmx {Abstractformat format;} xx_tiled_tmx;
XXFC_API void xx_tiled_tmx_init(xx_tiled_tmx *,xx_io_device *,int64_t);
XXFC_API xx_tiled_tmx *xx_tiled_tmx_create(xx_io_device *,int64_t);
XXFC_API void xx_tiled_tmx_destroy(xx_tiled_tmx *);
XXFC_API void xx_tiled_tmx_free(xx_tiled_tmx *);
XXFC_API bool xx_tiled_tmx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tiled_tmx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tiled_tmx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tiled_tmx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tiled_tmx_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
