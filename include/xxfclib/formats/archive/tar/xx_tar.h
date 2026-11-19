/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar.h @brief Uncompressed TAR archive reader and writer. */

#ifndef XXFCLIB_FORMAT_TAR_H
#define XXFCLIB_FORMAT_TAR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar xx_tar;
typedef struct xx_tar xx_tar_t;
typedef struct xx_tar XTar;

/* The ids are defined by src/datastructs/tar.xxds and must match it. */
typedef enum xx_tar_data_struct_id_e {
    XX_TAR_DS_UNKNOWN = 0,
    XX_TAR_DS_HEADER,
    XX_TAR_DS_DATA,
    XX_TAR_DS_PADDING,
    XX_TAR_DS_END_MARKERS
} xx_tar_data_struct_id_t;

struct xx_tar {
    Abstractformat format;      /**< Base format; must be first. */
    uint64_t number_of_records; /**< Visible archive entries. */
    uint64_t number_of_members; /**< Physical headers, including metadata. */
    int64_t archive_end;        /**< Absolute end of TAR data. */
    void *internal;             /**< Private parsed representation. */
};

XXFC_API void xx_tar_init(xx_tar *tar, xx_io_device *dev, int64_t base_address);
XXFC_API xx_tar *xx_tar_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_tar_free(xx_tar *tar);
XXFC_API void xx_tar_destroy(xx_tar *tar);

XXFC_API bool xx_tar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_tar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_tar_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tar_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tar_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tar_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Begin writing a POSIX USTAR archive to the format device.
 *
 * File and directory names are UTF-8. Paths longer than the 100-byte USTAR
 * name field are split at a directory separator into the 155-byte prefix and
 * 100-byte name fields when possible.
 */
XXFC_API xx_archive_write_state *xx_tar_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
/**
 * @brief Append one regular file or directory to a USTAR archive.
 *
 * Regular-file data is streamed from the beginning of @p source_dev. Passing
 * NULL creates an empty regular file. Directories are selected with
 * XX_META_ID_IS_FOLDER (or a trailing slash) and do not consume source data.
 * The header takes XX_META_ID_ATTRIBUTES (the mode; 0644, or 0755 for a
 * directory, when absent), XX_META_ID_TIMESTAMP (Unix seconds; 0 when absent),
 * and XX_META_ID_OWNER_ID and XX_META_ID_GROUP_ID (0 when absent); a value its
 * octal field cannot hold fails the record.
 */
XXFC_API bool xx_tar_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev,
                                         xx_pd_struct *pd);
/** @brief Write the two zero TAR end records and finish the archive. */
XXFC_API bool xx_tar_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
/** @brief Release a TAR archive writing state. */
XXFC_API void xx_tar_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state);

XXFC_API uint64_t xx_tar_get_number_of_records(const xx_tar *tar);
XXFC_API uint64_t xx_tar_get_number_of_members(const xx_tar *tar);
XXFC_API int64_t xx_tar_get_archive_end(const xx_tar *tar);

/* The compressed TAR readers (tar.gz, tar.bz2, tar.xz, tar.lzma, tar.lz,
 * tar.lz4, tar.lzo, tar.zst, tar.Z, npm) decode their stream once, into
 * RAM and beyond a threshold into a temporary file. For any other format
 * these return false. */
/** A cut stream, or a whole one whose TAR is cut (it ends in a member, or
 * without its end marker): the opened reader's members are those of the
 * decoded prefix, the last of which may have lost data (it is listed, and
 * fails). */
XXFC_API bool xx_tar_compressed_is_incomplete(const Abstractformat *format);
/** In such a prefix, the last member unpacked is the one the cut runs
 * through, or a hard link to it: it failed for that. */
XXFC_API bool xx_tar_compressed_last_cut(const Abstractformat *format);
/** Such a prefix holds members that no check of the stream vouches for:
 * those past the last gzip member, xz or bzip2 Block, zstd frame or LZ4
 * block or frame whose checksum was verified (with LZMA-alone, every
 * member). They are read without one, as GNU tar reads them. */
XXFC_API bool xx_tar_compressed_unverified(const Abstractformat *format);
/** In such a prefix, the last member unpacked is one of them, or a hard
 * link to one. */
XXFC_API bool xx_tar_compressed_last_unverified(const Abstractformat *format);
/** The reader failed only because its TAR is larger than the RAM it may
 * keep, *limit bytes: in a memory-only scope, which cannot spill it to a
 * temporary file, or when no temporary file can be made. */
XXFC_API bool xx_tar_compressed_too_large(const Abstractformat *format, uint64_t *limit);
/** The reader failed because its TAR, in a temporary file, grew past the
 * RAM maximum to more than *ratio times the size of its compressed input,
 * as a decompression bomb does. */
XXFC_API bool xx_tar_compressed_too_dense(const Abstractformat *format, uint32_t *ratio);
/** The reader failed because the temporary file holding its TAR could not
 * be written (a full disk). */
XXFC_API bool xx_tar_compressed_no_room(const Abstractformat *format);

static inline Abstractformat *xx_tar_to_format(xx_tar *tar)
{
    return tar ? &tar->format : NULL;
}

static inline const Abstractformat *xx_tar_to_format_const(const xx_tar *tar)
{
    return tar ? &tar->format : NULL;
}

static inline void XTar_init(xx_tar *tar, xx_io_device *dev, int64_t base_address)
{
    xx_tar_init(tar, dev, base_address);
}

static inline xx_tar *XTar_create(xx_io_device *dev, int64_t base_address)
{
    return xx_tar_create(dev, base_address);
}

static inline void XTar_free(xx_tar *tar)
{
    xx_tar_free(tar);
}

static inline xx_archive_write_state *XTar_create_archive_records_writing(xx_tar *tar, const xx_list_s *options, xx_pd_struct *pd)
{
    return tar ? xx_tar_create_archive_records_writing(&tar->format, options, pd) : NULL;
}

static inline bool XTar_pack_archive_record(xx_tar *tar, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd)
{
    return tar ? xx_tar_pack_archive_record(&tar->format, state, record, source_dev, pd) : false;
}

static inline bool XTar_finalize_archive_records_writing(xx_tar *tar, xx_archive_write_state *state, xx_pd_struct *pd)
{
    return tar ? xx_tar_finalize_archive_records_writing(&tar->format, state, pd) : false;
}

static inline void XTar_free_archive_records_writing(xx_tar *tar, xx_archive_write_state *state)
{
    if (tar) xx_tar_free_archive_records_writing(&tar->format, state);
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tar_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tar_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tar_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tar_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tar_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_TAR_H */
