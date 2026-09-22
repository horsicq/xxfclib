/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bagf.h
 *  @brief Novell NetWare "BAGF" bag file (.XDC, .RPC).
 */

/* WHERE THE LAYOUT COMES FROM.  U3's recognition predicate for its BAGF
 * handler (FUN_006742a0, reached from VMT slot 0 at 0x00674620) supplies the
 * signature and the version:
 *
 *   u32 @ 0x00 == 0x46474142  ("BAGF")
 *   u8  @ 0x04 == 2   and   u8 @ 0x05 == 0   (a little-endian version 2)
 *   i32 @ 0x08 >  0
 *
 * The rest was measured over F:\ARC\ARC\BAGF and then confirmed against the
 * reference unpacker's own output:
 *
 *   header, 112 bytes at offset 0, little endian:
 *     0x00  4    "BAGF"
 *     0x04  u32  version, 2
 *     0x08  u32  1 in every sample
 *     0x0c  20   zero in every sample
 *     0x20  u32  0 or 2
 *     0x24  u32  0 or 1
 *     0x28  u32  PAYLOAD SIZE in bytes
 *     0x2c  16   member name, length-prefixed (one count byte, then that
 *                many characters, the field zero padded to 16)
 *     0x3c  52   description, length-prefixed the same way
 *     0x70  ..   payload, exactly `payload size` bytes
 *
 * THE PAYLOAD IS STORED, NOT COMPRESSED.  In all five samples the payload
 * size equals the file size minus 112 exactly (16, 127179, 158178, 141260 and
 * 141372-112), and the reference unpacker's extraction of apache.xdc is 16
 * bytes - byte for byte the 16 bytes that sit at offset 112 of the file.  So
 * this reader copies the member out rather than pretending a decoder is
 * missing.
 *
 * THE MEMBER NAME is the length-prefixed string at 0x2c, which is what the
 * reference unpacker names its output after ("MPK_Bag" for the two .XDC,
 * "NWTIL.NLM" for the three .RPC).  The description at 0x3c ("MT Safe NLM",
 * "TIL descriptions for ioengine", ...) is published as the record comment.
 *
 * WHAT IS NOT KNOWN.  The u32 at 0x08 and the pair at 0x20/0x24 are not
 * identified.  0x08 is 1 everywhere, which is consistent with a member count
 * but cannot be verified from five files that all have one member; 0x20/0x24
 * are (2,0) for the .XDC pair and (0,1) for the .RPC trio, so they encode
 * some kind of content class.  All three are surfaced as opaque values and
 * none of them is used as a length or as a validity test beyond U3's own
 * "i32 at 0x08 is positive": a field that cannot be verified must not decide
 * whether a file is accepted.
 */

#ifndef XXFCLIB_FORMAT_BAGF_H
#define XXFCLIB_FORMAT_BAGF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Header magic and size. */
#define XX_BAGF_SIGNATURE "BAGF"
#define XX_BAGF_SIGNATURE_SIZE 4U
#define XX_BAGF_HEADER_SIZE 112U
/** Only version ever seen. */
#define XX_BAGF_VERSION 2U
/** Length-prefixed string fields: offset and total field width. */
#define XX_BAGF_NAME_OFFSET 0x2cU
#define XX_BAGF_NAME_FIELD 16U
#define XX_BAGF_COMMENT_OFFSET 0x3cU
#define XX_BAGF_COMMENT_FIELD 52U

typedef struct xx_bagf xx_bagf;
typedef struct xx_bagf xx_bagf_t;
typedef struct xx_bagf XBagf;

struct xx_bagf {
    Abstractformat format;  /**< Base format structure (first member). */
    int64_t data_offset;    /**< Absolute offset of the payload. */
    int64_t data_size;      /**< Payload size, bounded to the file. */
    uint32_t opaque_08;     /**< u32 at 0x08.  Never interpreted. */
    uint32_t opaque_20;     /**< u32 at 0x20.  Never interpreted. */
    uint32_t opaque_24;     /**< u32 at 0x24.  Never interpreted. */
};

XXFC_API void xx_bagf_init(xx_bagf *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_bagf *xx_bagf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bagf_destroy(xx_bagf *archive);
XXFC_API void xx_bagf_free(xx_bagf *archive);

XXFC_API bool xx_bagf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bagf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bagf_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_bagf_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_bagf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bagf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bagf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bagf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bagf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute payload offset, or -1 before handle_base_info. */
XXFC_API int64_t xx_bagf_get_data_offset(const xx_bagf *archive);
/** Payload size, or -1 before handle_base_info. */
XXFC_API int64_t xx_bagf_get_data_size(const xx_bagf *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BAGF_H */
