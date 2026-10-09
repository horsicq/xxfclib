/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xamarin_compressed_assembly.h
 *  @brief Xamarin.Android compressed assembly ("XALZ") reader. */

#ifndef XXFCLIB_FORMAT_XAMARIN_COMPRESSED_ASSEMBLY_H
#define XXFCLIB_FORMAT_XAMARIN_COMPRESSED_ASSEMBLY_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A .NET assembly compressed by the Xamarin.Android build
 * (assemblies/<name>.dll inside an APK, or an entry of assemblies.blob).
 *
 *   0x00  char[4]  "XALZ"
 *   0x04  u32 LE   descriptor index (slot in the app's assembly table)
 *   0x08  u32 LE   uncompressed size
 *   0x0C  one raw LZ4 block (LZ4_compress_default output: sequences only,
 *         no frame, no length prefix, no checksum)
 *
 * The container does not store the packed length.  The reader recovers it by
 * walking the LZ4 sequence grammar until exactly `uncompressed size` bytes
 * are produced by a final literal-only sequence, so the format size is exact
 * even when the stream is followed by other data.  The single member is the
 * decoded assembly (a PE image).
 */
typedef struct xx_xamarin_compressed_assembly {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t descriptor_index;
    uint32_t unpacked_size;
    int64_t packed_size; /**< Bytes of the LZ4 block after the header. */
} xx_xamarin_compressed_assembly;

typedef xx_xamarin_compressed_assembly xx_xamarin_compressed_assembly_t;

XXFC_API void xx_xamarin_compressed_assembly_init(
    xx_xamarin_compressed_assembly *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_xamarin_compressed_assembly *xx_xamarin_compressed_assembly_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_xamarin_compressed_assembly_destroy(
    xx_xamarin_compressed_assembly *archive);
XXFC_API void xx_xamarin_compressed_assembly_free(
    xx_xamarin_compressed_assembly *archive);

XXFC_API bool xx_xamarin_compressed_assembly_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xamarin_compressed_assembly_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xamarin_compressed_assembly_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_xamarin_compressed_assembly_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_xamarin_compressed_assembly_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_xamarin_compressed_assembly_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xamarin_compressed_assembly_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xamarin_compressed_assembly_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xamarin_compressed_assembly_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xamarin_compressed_assembly_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_xamarin_compressed_assembly_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xamarin_compressed_assembly_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xamarin_compressed_assembly_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_xamarin_compressed_assembly_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_XAMARIN_COMPRESSED_ASSEMBLY_H */
