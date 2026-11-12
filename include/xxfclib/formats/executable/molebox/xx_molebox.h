/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_MOLEBOX_H
#define XX_MOLEBOX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Static Win32 SVFS/SPACK package reader. Public filenames are preserved;
 * hidden names use the stored MD5 path signature. Uses supplied passwords or
 * actual embedded mount keys. Does not execute packaged programs. */
typedef struct xx_molebox {
    Abstractformat format;
    void *index;
    uint64_t generation;
} xx_molebox;
XXFC_API void xx_molebox_init(xx_molebox *, xx_io_device *, int64_t);
XXFC_API xx_molebox *xx_molebox_create(xx_io_device *, int64_t);
XXFC_API void xx_molebox_destroy(xx_molebox *);
XXFC_API void xx_molebox_free(xx_molebox *);
/** Physical metadata offsets are absolute device coordinates. Encrypted
 * fields are not exposed as plaintext source members. catalog_offset is -1
 * for a standalone package; catalog_size excludes its eight-byte footer. */
typedef struct xx_molebox_physical_metadata {
    uint32_t package_count;
    int64_t package_offset, tree_offset, header_offset, catalog_offset;
    uint32_t tree_size, catalog_size;
    bool has_verified_prefix;
} xx_molebox_physical_metadata;
/** Validates native identity, prepares metadata when necessary and preserves
 * the input cursor. Rejects foreign readers and out-of-range package indices.
 * The complete physical package header occupies 48 encrypted bytes. */
XXFC_API bool xx_molebox_get_physical_metadata(Abstractformat *, uint32_t package_index,
                                              xx_molebox_physical_metadata *, xx_pd_struct *);
/** Cursor-preserving EOF catalog-key probe; validate before detection. */
XXFC_API bool xx_molebox_has_candidate_device(xx_io_device *, int64_t);
/** Validate a header-encrypted standalone package with a supplied password or
 * reusable molebox-md5:<32hex> credential. Preserves the device cursor. */
XXFC_API xx_file_type_t xx_molebox_detect_device(xx_io_device *, const char *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_molebox_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_molebox_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_molebox_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_molebox_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_molebox_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
