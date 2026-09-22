/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_xorarchive.h
 * @brief XOR-masked ZIP and ARJ containers.
 *
 * An ordinary ZIP or ARJ archive whose every byte has been passed through a
 * fixed, position-independent byte mask so that naive scanners no longer see
 * the container magic.  The mask observed in the wild is
 *
 *     stored = rotate_left(original, rotation) ^ key
 *
 * with @c rotation 0 (a plain single-byte XOR) for the ZIP family and
 * @c rotation 5 for the ARJ family.  The reader recovers @c rotation and
 * @c key from the known plaintext of the container magic, exposes the
 * unmasked bytes through a private I/O device, and delegates every parsing,
 * listing and extraction operation to the existing xx_zip / xx_arj readers.
 * No ZIP or ARJ parsing logic is duplicated here.
 */

#ifndef XXFCLIB_FORMAT_XORARCHIVE_H
#define XXFCLIB_FORMAT_XORARCHIVE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/formats/arj/xx_arj.h"
#include "xxfclib/formats/zip/xx_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_xorarchive xx_xorarchive;
typedef struct xx_xorarchive xx_xorarchive_t;
typedef struct xx_xorarchive XXorArchive;

/** Container family recovered from the unmasked magic. */
typedef enum xx_xorarchive_family_e {
    XX_XORARCHIVE_FAMILY_NONE = 0, /**< Nothing recognizable; reader is inert. */
    XX_XORARCHIVE_FAMILY_ZIP = 1,  /**< Unmasks to a PK\3\4 local file header. */
    XX_XORARCHIVE_FAMILY_ARJ = 2   /**< Unmasks to a 60 EA main header. */
} xx_xorarchive_family_t;

/**
 * @brief XOR-masked archive object.
 *
 * The embedded container union must remain first: both alternatives start
 * with Abstractformat, so a xx_xorarchive pointer casts straight to the
 * vtable regardless of which family was detected.
 */
struct xx_xorarchive {
    union xx_xorarchive_container {
        Abstractformat format; /**< Common base of both alternatives. */
        xx_zip zip;
        xx_arj arj;
    } container;

    xx_io_device view;      /**< Unmasking 1:1 view over @c source. */
    xx_io_device *source;   /**< Borrowed masked device; never closed here. */
    uint8_t table[256];     /**< Precomputed stored-byte -> original-byte map. */
    uint8_t key;            /**< Recovered XOR key. */
    uint8_t rotation;       /**< Recovered left-rotation applied before the XOR. */
    uint8_t family;         /**< xx_xorarchive_family_t value. */
};

XXFC_API void xx_xorarchive_init(xx_xorarchive *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_xorarchive *xx_xorarchive_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_xorarchive_destroy(xx_xorarchive *archive);
XXFC_API void xx_xorarchive_free(xx_xorarchive *archive);

XXFC_API bool xx_xorarchive_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_xorarchive_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);

/**
 * @brief Cheap prefilter over the dispatcher's 64-byte magic window.
 *
 * True when the window could be a masked ZIP or ARJ header: a mask candidate
 * exists and the bytes it uncovers still look like a container header.  This
 * is only a prefilter; identity is decided by xx_xorarchive_check_is_valid(),
 * which requires the whole container to parse.
 */
XXFC_API bool xx_xorarchive_test_magic(const uint8_t *magic, size_t magic_size);

/** Recovered mask, valid only after a successful validity check. */
XXFC_API uint8_t xx_xorarchive_get_key(const xx_xorarchive *archive);
XXFC_API uint8_t xx_xorarchive_get_rotation(const xx_xorarchive *archive);
XXFC_API xx_xorarchive_family_t xx_xorarchive_get_family(
    const xx_xorarchive *archive);

/* Cast helpers */
static inline Abstractformat *xx_xorarchive_to_format(xx_xorarchive *archive) {
    return archive ? &archive->container.format : NULL;
}

static inline const Abstractformat *xx_xorarchive_to_format_const(
    const xx_xorarchive *archive) {
    return archive ? &archive->container.format : NULL;
}

static inline void XXorArchive_init(xx_xorarchive *archive,
                                    xx_io_device *device,
                                    int64_t base_address) {
    xx_xorarchive_init(archive, device, base_address);
}

static inline xx_xorarchive *XXorArchive_create(xx_io_device *device,
                                                int64_t base_address) {
    return xx_xorarchive_create(device, base_address);
}

static inline void XXorArchive_free(xx_xorarchive *archive) {
    xx_xorarchive_free(archive);
}

static inline bool XXorArchive_is_valid(xx_xorarchive *archive,
                                        xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->container.format, pd) : false;
}

static inline bool XXorArchive_handle_base_info(xx_xorarchive *archive,
                                                xx_pd_struct *pd) {
    return archive ? xx_format_handle_base_info(&archive->container.format, pd)
                   : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XORARCHIVE_H */
