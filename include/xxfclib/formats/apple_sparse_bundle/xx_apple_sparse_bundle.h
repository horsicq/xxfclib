/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_apple_sparse_bundle.h @brief Apple sparse bundle (.sparsebundle) reader. */

/* An Apple sparse bundle (UDSB) is a DIRECTORY, not a file:
 *
 *   name.sparsebundle/
 *     Info.plist      XML property list: diskimage-bundle-type =
 *                     com.apple.diskimage.sparsebundle, band-size, size,
 *                     bundle-backingstore-version
 *     Info.bckup      copy of Info.plist
 *     token           empty, or the "encrcdsa" header of an encrypted image
 *     bands/<hex>     band N holds disk bytes [N * band-size, (N+1) * band-size);
 *                     the name is N in lower-case hex without leading zeros.
 *                     A missing band, or the part of a band past the end of
 *                     its file, reads as zeros.
 *
 * The reader's device is the Info.plist stream. That alone identifies the
 * format (detection, band size, disk size). The band files are siblings of
 * the plist, and an xx_io_device carries no path, so the disk image can only
 * be reconstructed once the reader knows the bundle directory: call
 * xx_apple_sparse_bundle_set_bundle_path(), or open the bundle with
 * xx_apple_sparse_bundle_open_path(). Until then the reader is not an
 * archive (no records). With the directory known it publishes ONE member,
 * "disk.img", of the plist's declared size.
 *
 * Encrypted bundles (token starts with "encrcdsa") are reported through
 * is_crypted and are not extracted.
 */

#ifndef XXFCLIB_FORMAT_APPLE_SPARSE_BUNDLE_H
#define XXFCLIB_FORMAT_APPLE_SPARSE_BUNDLE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_apple_sparse_bundle xx_apple_sparse_bundle;
typedef struct xx_apple_sparse_bundle xx_apple_sparse_bundle_t;
typedef struct xx_apple_sparse_bundle XAppleSparseBundle;

struct xx_apple_sparse_bundle {
    Abstractformat format;
    uint64_t number_of_records;   /**< 1 once the bundle directory is known, else 0. */
    uint64_t media_size;          /**< Disk size in bytes ("size"). */
    uint64_t band_size;           /**< Bytes per band file ("band-size"). */
    uint64_t number_of_bands;     /**< ceil(media_size / band_size). */
    uint32_t backingstore_version;/**< "bundle-backingstore-version", 0 if absent. */
    bool is_encrypted;            /**< token file carries an "encrcdsa" header. */
    bool owns_device;             /**< Device opened by _open_path; closed by _free. */
    char *bundle_path;            /**< Owned UTF-8 bundle directory, or NULL. */
    void *internal;
};

XXFC_API void xx_apple_sparse_bundle_init(xx_apple_sparse_bundle *bundle,
                                          xx_io_device *dev,
                                          int64_t base_address);
XXFC_API xx_apple_sparse_bundle *xx_apple_sparse_bundle_create(
    xx_io_device *dev, int64_t base_address);
XXFC_API void xx_apple_sparse_bundle_destroy(xx_apple_sparse_bundle *bundle);
XXFC_API void xx_apple_sparse_bundle_free(xx_apple_sparse_bundle *bundle);

/** Tell the reader where the bundle directory (the one holding Info.plist
 * and bands/) is. Resets the parsed state. Returns false on bad input or
 * allocation failure. NULL clears it. */
XXFC_API bool xx_apple_sparse_bundle_set_bundle_path(
    xx_apple_sparse_bundle *bundle, const char *bundle_path);

/** Open a bundle by path: either the .sparsebundle directory (Info.plist,
 * or Info.bckup when Info.plist is missing, is read from it) or the path of
 * its Info.plist / Info.bckup. The returned reader owns its device and must
 * be released with xx_apple_sparse_bundle_free(). NULL if nothing opens;
 * validity is NOT checked here. */
XXFC_API xx_apple_sparse_bundle *xx_apple_sparse_bundle_open_path(
    const char *path);

XXFC_API bool xx_apple_sparse_bundle_check_is_valid(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API bool xx_apple_sparse_bundle_handle_base_info(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API int64_t xx_apple_sparse_bundle_get_format_size(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_apple_sparse_bundle_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_apple_sparse_bundle_create_archive_records_reading(Abstractformat *self,
                                                      const xx_list_s *options,
                                                      xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_apple_sparse_bundle_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apple_sparse_bundle_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apple_sparse_bundle_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_apple_sparse_bundle_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_apple_sparse_bundle_get_media_size(
    const xx_apple_sparse_bundle *bundle);
XXFC_API uint64_t xx_apple_sparse_bundle_get_band_size(
    const xx_apple_sparse_bundle *bundle);

static inline Abstractformat *xx_apple_sparse_bundle_to_format(
    xx_apple_sparse_bundle *bundle) {
    return bundle ? &bundle->format : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_apple_sparse_bundle_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_apple_sparse_bundle_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apple_sparse_bundle_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apple_sparse_bundle_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_apple_sparse_bundle_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_APPLE_SPARSE_BUNDLE_H */
