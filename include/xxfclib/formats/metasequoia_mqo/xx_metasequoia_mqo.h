/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.metaseq.net/en/format.html
 * Metasequoia MQO1.0/1.1 ASCII text meshes: complete text vertices and polygon V/M/UV fields, local indexes/material references, finite typed properties, classic material/scene/light and RGB24 raw-hex thumbnail chunks. Original sections exported. NonASCII locale text, CodePage/MaterialEx/binary vertices/other extensions and external loading declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_METASEQUOIA_MQO_H
#define XX_METASEQUOIA_MQO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_metasequoia_mqo {Abstractformat format;} xx_metasequoia_mqo;
XXFC_API void xx_metasequoia_mqo_init(xx_metasequoia_mqo *,xx_io_device *,int64_t);
XXFC_API xx_metasequoia_mqo *xx_metasequoia_mqo_create(xx_io_device *,int64_t);
XXFC_API void xx_metasequoia_mqo_destroy(xx_metasequoia_mqo *);
XXFC_API void xx_metasequoia_mqo_free(xx_metasequoia_mqo *);
XXFC_API bool xx_metasequoia_mqo_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_metasequoia_mqo_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
