/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wrzl.h
 *  @brief "WRZL" single-stream compressed file.
 */

/* WRZL carries Kurt Haenen's LZRW1/KH blocks (published in SWAG
 * ARCHIVES/0041.PAS). The eight-byte lead is "WRZL" plus u32 total decoded
 * size. Each following block has u16 little-endian packed length and starts
 * with 0x40 (coded) or 0x80 (copied). Each block yields 32 KiB, except the
 * last, whose length follows from the total size. The 0x40 block uses
 * high-bit-first, 16-token control words, 12-bit backward distances, and a
 * repeated-byte escape. All six local corpus files decode to the declared
 * size; DUMMY.DA$ matches reference output byte-for-byte.
 *
 * The container has no member name; the reader uses a neutral output leaf.
 */

#ifndef XXFCLIB_FORMAT_WRZL_H
#define XXFCLIB_FORMAT_WRZL_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Header magic and size, before the first length-prefixed block. */
#define XX_WRZL_SIGNATURE "WRZL"
#define XX_WRZL_SIGNATURE_SIZE 4U
#define XX_WRZL_HEADER_SIZE 8U
/** Ceiling on the declared uncompressed size. */
#define XX_WRZL_MAX_UNCOMPRESSED_SIZE ((int64_t)1024 * 1024 * 1024)

typedef struct xx_wrzl xx_wrzl;
typedef struct xx_wrzl xx_wrzl_t;
typedef struct xx_wrzl XWrzl;

struct xx_wrzl {
    Abstractformat format; /**< Base format structure (first member). */
    int64_t packed_offset; /**< Absolute offset of the packed stream. */
    int64_t packed_size;   /**< Packed length, measured from the file. */
    int64_t unpacked_size; /**< Stored uncompressed length (u32 at 0x04). */
    uint16_t opaque_08;    /**< First block's packed length. */
    uint8_t mode;          /**< First block's 0x40/0x80 codec marker. */
    uint8_t flags;         /**< First block's next byte, retained for ABI. */
};

XXFC_API void xx_wrzl_init(xx_wrzl *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_wrzl *xx_wrzl_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_wrzl_destroy(xx_wrzl *archive);
XXFC_API void xx_wrzl_free(xx_wrzl *archive);

XXFC_API bool xx_wrzl_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wrzl_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_wrzl_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_wrzl_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_wrzl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wrzl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wrzl_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wrzl_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wrzl_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the packed stream, or -1 before handle_base_info. */
XXFC_API int64_t xx_wrzl_get_packed_offset(const xx_wrzl *archive);
/** Packed length, or -1 before handle_base_info. */
XXFC_API int64_t xx_wrzl_get_packed_size(const xx_wrzl *archive);
/** Stored uncompressed length, or -1 before handle_base_info. */
XXFC_API int64_t xx_wrzl_get_unpacked_size(const xx_wrzl *archive);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_wrzl_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_wrzl_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_wrzl_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_wrzl_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_wrzl_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_WRZL_H */
