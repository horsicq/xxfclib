/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wim.h @brief Microsoft Windows Imaging (WIM) container. */

#ifndef XXFCLIB_FORMAT_WIM_H
#define XXFCLIB_FORMAT_WIM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A WIM, a split WIM part (SWM) or a solid ESD.  Members are the files and
 * directories of every image (under "<index>/" when there are several),
 * named data streams as "<file>.__streams__/<name>", the XML resource as
 * "wim.xml", and any stream no image references as "stream_NNNN.bin".
 * Streams are content addressed by SHA-1 and may be stored or XPRESS, LZX or
 * LZMS coded (LZMS in solid resources too). */
typedef struct xx_wim {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_wim;

typedef struct xx_wim xx_wim_t;

XXFC_API void xx_wim_init(xx_wim *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_wim *xx_wim_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_wim_destroy(xx_wim *archive);
XXFC_API void xx_wim_free(xx_wim *archive);
XXFC_API bool xx_wim_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wim_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_wim_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_wim_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_wim_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wim_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wim_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wim_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wim_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/* Writer option XX_META_ID_COMPRESSION_METHOD accepts these numbers or the
 * matching lower-case names. The absent option preserves stored output. */
#define XX_WIM_COMPRESSION_STORED 0U
#define XX_WIM_COMPRESSION_XPRESS 1U
#define XX_WIM_COMPRESSION_LZX 2U
#define XX_WIM_COMPRESSION_LZMS 3U

/* Create a single-image WIM with streamed resources and SHA-1 verification.
 * XPRESS/LZX/LZMS use independent 32KiB chunks and the separately installed,
 * source-distributed wimlib compressor helper. The native library never links
 * wimlib or stages payloads to files. Missing runtime/over-budget compression
 * fails before the first destination write; stored writing remains portable.
 * COMPRESSION_LEVEL is 0/default or 1..100; nonzero stored levels are invalid.
 * OPT_MEMORY_LIMIT defaults to 256MiB, with a 256KiB transport reserve and
 * remaining workspace shared between native allocations and the helper.
 * Hosted helper runtime overhead is separately capped at 32MiB. MAX_MEMBER_SIZE
 * applies to input files. Caller-owned source devices retain their cursor.
 * Names may contain relative subdirectories; missing parents are synthesized.
 * Equal streams share a lookup resource. Encryption, links, alternate data
 * streams and security descriptors are not synthesized by this writer. */
XXFC_API xx_archive_write_state *xx_wim_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_wim_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev,
                                         xx_pd_struct *pd);
XXFC_API bool xx_wim_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_wim_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state);

static inline Abstractformat *xx_wim_to_format(xx_wim *archive)
{
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_wim_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_wim_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_wim_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_wim_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_wim_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_WIM_H */
