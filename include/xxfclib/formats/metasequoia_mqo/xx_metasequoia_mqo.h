/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.metaseq.net/en/format.html
 * Metasequoia MQO1.0/1.1 ASCII text meshes: complete text vertices and polygon V/M/UV fields, local indexes/material references, finite typed properties, classic
 * material/scene/light and RGB24 raw-hex thumbnail chunks. Original sections exported. NonASCII locale text, CodePage/MaterialEx/binary vertices/other extensions and
 * external loading declined. Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_METASEQUOIA_MQO_H
#define XX_METASEQUOIA_MQO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_metasequoia_mqo {
    Abstractformat format;
} xx_metasequoia_mqo;
XXFC_API void xx_metasequoia_mqo_init(xx_metasequoia_mqo *, xx_io_device *, int64_t);
XXFC_API xx_metasequoia_mqo *xx_metasequoia_mqo_create(xx_io_device *, int64_t);
XXFC_API void xx_metasequoia_mqo_destroy(xx_metasequoia_mqo *);
XXFC_API void xx_metasequoia_mqo_free(xx_metasequoia_mqo *);
XXFC_API bool xx_metasequoia_mqo_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_metasequoia_mqo_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_metasequoia_mqo_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_metasequoia_mqo_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_metasequoia_mqo_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_metasequoia_mqo_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_metasequoia_mqo_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
