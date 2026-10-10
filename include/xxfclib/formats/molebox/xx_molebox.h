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
/** Cursor-preserving EOF catalog-key probe; validate before detection. */
XXFC_API bool xx_molebox_has_candidate_device(xx_io_device *, int64_t);
/** Validate a header-encrypted standalone package with a supplied password or
 * reusable molebox-md5:<32hex> credential. Preserves the device cursor. */
XXFC_API xx_file_type_t xx_molebox_detect_device(xx_io_device *, const char *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
