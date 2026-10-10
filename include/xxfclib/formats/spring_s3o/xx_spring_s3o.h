/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/spring/spring/develop/rts/Rendering/Models/s3o.h
 * Spring S3O version0 triangle/quad pieces with up to256 pieces, depth32,65536 vertices and262144 indices per piece. Validates disjoint headers/strings/tables, finite
 * geometry, child cycles and vertex references. Exports piece headers, vertex and index buffers; triangle strips, collision records, texture loading and rendering
 * unsupported.
 */
#ifndef XX_SPRING_S3O_H
#define XX_SPRING_S3O_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_spring_s3o {
    Abstractformat format;
} xx_spring_s3o;
XXFC_API void xx_spring_s3o_init(xx_spring_s3o *, xx_io_device *, int64_t);
XXFC_API xx_spring_s3o *xx_spring_s3o_create(xx_io_device *, int64_t);
XXFC_API void xx_spring_s3o_destroy(xx_spring_s3o *);
XXFC_API void xx_spring_s3o_free(xx_spring_s3o *);
XXFC_API bool xx_spring_s3o_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_spring_s3o_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_spring_s3o_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_spring_s3o_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_spring_s3o_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_spring_s3o_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_spring_s3o_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
