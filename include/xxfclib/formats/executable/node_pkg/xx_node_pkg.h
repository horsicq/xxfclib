/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Node.js single executables made by pkg (vercel/pkg, yao-pkg): a patched
 * node binary with the project's files appended and described by the
 * JavaScript prelude that ends the file.
 */
#ifndef XX_NODE_PKG_H
#define XX_NODE_PKG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_node_pkg {
    Abstractformat format;
} xx_node_pkg;
XXFC_API void xx_node_pkg_init(xx_node_pkg *, xx_io_device *, int64_t);
XXFC_API xx_node_pkg *xx_node_pkg_create(xx_io_device *, int64_t);
XXFC_API void xx_node_pkg_destroy(xx_node_pkg *);
XXFC_API void xx_node_pkg_free(xx_node_pkg *);
XXFC_API bool xx_node_pkg_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_node_pkg_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_node_pkg_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_node_pkg_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_node_pkg_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_node_pkg_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_node_pkg_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
