/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qip2.h
 *  @brief Quarterdeck install archive, version 2 (.QIP / .QIF).
 */

/* WHERE THE LAYOUT COMES FROM.  Two independent sources agree.
 *
 * U3's recognition predicate for its QIP2 handler (FUN_0053a0b0, reached from
 * VMT slot 0 at 0x0053a540) pins the header exactly:
 *
 *   u16 @ 0x00 == 0x5051      ("QP")
 *   u16 @ 0x02 != 0           member count
 *   i32 @ 0x04 >  0           index size in bytes
 *   (u32 @ 0x04 & 0xf) == 0   index size is a multiple of 16
 *   (u32 @ 0x04 >> 4) == u16 @ 0x02   i.e. index size == 16 * count
 *
 * XArchive's archives/xqip1.cpp gives the member record of the VERSION 1
 * format, and the version 2 record is that same record with one extra u32
 * inserted before the uncompressed size:
 *
 *   header, 16 bytes:
 *     0x00  2    "QP"
 *     0x02  u16  member count
 *     0x04  u32  index size, always 16 * count
 *     0x08  u32  2 in every sample seen (the version)
 *     0x0c  u32  0
 *
 *   index entry, 16 bytes, starting at 0x10:
 *     0x00  u32  ABSOLUTE offset of the member's "QD" record
 *     0x04  12   member name, NUL terminated inside the field
 *
 *   member record ("QD"), 36 bytes, at that offset:
 *     0x00  2    "QD"
 *     0x02  u16  kind; 0 = file.  1 = a PATH record, which holds an install
 *                destination rather than a member and never appears in the
 *                index, so this reader never sees one through the index.
 *     0x04  u32  packed size
 *     0x08  u16  sequence number, 1-based
 *     0x0a  u8   flags
 *     0x0b  u16  DOS time
 *     0x0d  u16  DOS date
 *     0x0f  u32  unidentified; NOT the uncompressed size (see below)
 *     0x13  u32  uncompressed size
 *     0x17  13   member name, NUL terminated inside the field
 *     0x24  ..   packed bytes, `packed size` of them
 *
 * Members are PKWARE DCL ("implode") streams: the first two payload bytes are
 * always the DCL literal-mode byte and dictionary-size byte, and the library's
 * existing DCL decoder handles them.
 *
 * CONFIRMED AGAINST THE CORPUS.  All 232 files in F:\ARC\ARC\QIP2 satisfy
 * every rule above with nothing left over: each index offset lands on a "QD"
 * signature, every kind field is 0, every packed extent lies inside the file,
 * and the highest member end equals the file size exactly in all 232 - so the
 * members tile the file with no gap and no trailer.
 *
 * WHAT IS NOT KNOWN.  The u32 at 0x0f of the member record is not identified.
 * It is NOT the uncompressed size (that is the next field, and it matches the
 * DCL decode), and it is not a CRC of the packed bytes under any of the
 * standard polynomials.  It is surfaced through the metadata as an opaque
 * value and never used as a length or a validity test: a field that cannot be
 * verified must not be allowed to reject a file.
 *
 * NAMES.  The index and the member record carry the same name in every sample
 * checked.  The member record's field is the wider of the two (13 bytes
 * against 12), so it is preferred and the index name is the fallback.
 */

#ifndef XXFCLIB_FORMAT_QIP2_H
#define XXFCLIB_FORMAT_QIP2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Archive header magic and size. */
#define XX_QIP2_SIGNATURE "QP"
#define XX_QIP2_SIGNATURE_SIZE 2U
#define XX_QIP2_HEADER_SIZE 16U
/** Index entry size and its name field width. */
#define XX_QIP2_INDEX_ENTRY_SIZE 16U
#define XX_QIP2_INDEX_NAME_SIZE 12U
/** Member record magic, size and name field width. */
#define XX_QIP2_RECORD_SIGNATURE "QD"
#define XX_QIP2_RECORD_SIZE 36U
#define XX_QIP2_RECORD_NAME_SIZE 13U
/** Member record kinds. */
#define XX_QIP2_KIND_FILE 0U
#define XX_QIP2_KIND_PATH 1U
/** Ceiling on a declared uncompressed size. */
#define XX_QIP2_MAX_UNCOMPRESSED_SIZE ((int64_t)1024 * 1024 * 1024)

typedef struct xx_qip2 xx_qip2;
typedef struct xx_qip2 xx_qip2_t;
typedef struct xx_qip2 XQip2;

struct xx_qip2 {
    Abstractformat format;      /**< Base format structure (first member). */
    uint64_t number_of_records; /**< Member count taken from the header. */
    uint32_t format_version;    /**< u32 at 0x08; 2 in every known build. */
};

XXFC_API void xx_qip2_init(xx_qip2 *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_qip2 *xx_qip2_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_qip2_destroy(xx_qip2 *archive);
XXFC_API void xx_qip2_free(xx_qip2 *archive);

XXFC_API bool xx_qip2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_qip2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_qip2_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_qip2_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_qip2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_qip2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qip2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qip2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qip2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QIP2_H */
